//
// jsd_circle.cpp
//
// EtherCAT bus layer of the Circle port of JSD
//
// This file replaces src/jsd.c of JSD. It provides the same API, but is
// implemented on top of the SOEM 2.0 port of Circle (addon/soem) instead of
// SOEM 1.4, and uses Circle tasks instead of POSIX threads.
//
// The differences to the original implementation are:
//
// * SOEM 2.0 embeds the slave list, group list and the internal buffers in
//   ecx_contextt, so jsd_alloc() does not have to allocate them one by one.
// * The overlapping IO map is selected with the context flag overlappedMode,
//   instead of using the separate ecx_*_overlap_* functions.
// * Only the Elmo device drivers are supported (jsd_egd and jsd_epd_nominal).
// * The transition to the OPERATIONAL state exchanges process data
//   continuously, while it waits for the state change, instead of waiting for
//   up to EC_TIMEOUTSTATE without sending anything.
// * The mailbox handler of SOEM 2.0 is pumped from jsd_read(), which allows
//   SDO transfers from other tasks without stalling the process data cycle.
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2026  R. Stange <rsta2@gmx.net>
//
// JSD (JPL Servo Drive) is developed by the Jet Propulsion Laboratory,
// California Institute of Technology, and is distributed under a 3-clause BSD
// license. See the file LICENSE in the JSD sources.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <jsd/jsd.h>
#include <jsd/jsd_sdo.h>
#include <jsd/jsd_egd.h>
#include <jsd/jsd_epd_nominal.h>
#include <jsd/jsd_circle.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/string.h>
#include <circle/util.h>
#include <circle/alloc.h>
#include <assert.h>

// number of process data cycles per attempt to reach the OPERATIONAL state
#define JSD_PO2OP_CYCLES_PER_ATTEMPT	200

// timeout for one state check, while the bus is going operational (us)
#define JSD_PO2OP_STATECHECK_TIMEOUT	50000

/****************************************************
 * Public functions
 ****************************************************/

jsd_t *jsd_alloc (void)
{
	jsd_t *self = (jsd_t *) calloc (1, sizeof (jsd_t));
	if (self == 0)
	{
		ERROR ("Cannot allocate the JSD context");

		return 0;
	}

	// SOEM 2.0 holds the slave list, group list and all internal buffers in
	// the context itself, so nothing has to be allocated here. Only the
	// pointer to the slave configurations is needed, because the PO2SO
	// callbacks get it from the context.
	self->ecx_context.userdata = (void *) &self->slave_configs;

	// JSD maps the process data with overlapping inputs and outputs
	self->ecx_context.overlappedMode = TRUE;

	return self;
}

void jsd_set_slave_config (jsd_t *self, uint16_t slave_id,
			   jsd_slave_config_t slave_config)
{
	assert (self);
	assert (slave_id < EC_MAXSLAVE);

	if (self->init_complete)
	{
		ERROR ("slave configuration must be set before jsd_init() is called");
	}
	assert (!self->init_complete);

	self->slave_configs[slave_id] = slave_config;
}

bool jsd_init (jsd_t *self, const char *ifname, uint8_t enable_autorecovery,
	       int timeout_us)
{
	assert (self);

	self->enable_autorecovery = enable_autorecovery;

	if (ecx_init (&self->ecx_context, ifname) <= 0)
	{
		ERROR ("Unable to open the net device %s", ifname);

		return false;
	}

	MSG ("ecx_init on %s succeeded", ifname);

	// find and auto-config slaves
	if (ecx_config_init (&self->ecx_context) <= 0)
	{
		WARNING ("No slaves found on %s", ifname);

		return false;
	}

	MSG ("%d slaves found on bus", self->ecx_context.slavecount);

	// the PO2SO callbacks must be registered before the PDOs are mapped
	if (!jsd_init_all_devices (self))
	{
		ERROR ("Could not init all devices");

		return false;
	}

	// configure the IO map, this triggers the PO2SO callbacks
	int iomap_size = ecx_config_map_group (&self->ecx_context, self->IOmap, 0);
	if (iomap_size <= 0)
	{
		ERROR ("Could not map the process data");

		return false;
	}

	if (iomap_size > (int) sizeof self->IOmap)
	{
		ERROR ("IO Map is not large enough for this application (%d > %u)",
		       iomap_size, (unsigned) sizeof self->IOmap);

		return false;
	}

	// Report the mapped process data sizes and compare them with the sizes,
	// which the device driver expects. The drivers assert on this in their
	// read and process functions, which would halt the application in the
	// cyclic path. Checking it here fails jsd_init() with a message instead.
	int sid;
	for (sid = 1; sid <= self->ecx_context.slavecount; sid++)
	{
		jsd_slave_config_t *config = &self->slave_configs[sid];

		unsigned mapped_in = self->ecx_context.slavelist[sid].Ibytes;
		unsigned mapped_out = self->ecx_context.slavelist[sid].Obytes;

		unsigned expected_in = 0;
		unsigned expected_out = 0;

		if (config->configuration_active)
		{
			switch (config->driver_type)
			{
			case JSD_DRIVER_TYPE_EGD:
				expected_in = sizeof (jsd_egd_txpdo_data_t);
				expected_out =
					  config->egd.drive_cmd_mode
					    == JSD_EGD_DRIVE_CMD_MODE_CS
					? sizeof (jsd_egd_rxpdo_data_cs_mode_t)
					: sizeof (jsd_egd_rxpdo_data_profiled_mode_t);
				break;

			case JSD_DRIVER_TYPE_EPD_NOMINAL:
				expected_in = sizeof (jsd_epd_nominal_txpdo_data_t);
				expected_out = sizeof (jsd_epd_nominal_rxpdo_data_t);
				break;

			default:
				break;
			}
		}

		if (expected_in == 0)
		{
			MSG ("slave[%d] mapped %u input and %u output byte(s)",
			     sid, mapped_in, mapped_out);

			continue;
		}

		if (   mapped_in != expected_in
		    || mapped_out != expected_out)
		{
			ERROR ("slave[%d] mapped %u input and %u output byte(s), "
			       "but the driver requires %u and %u. The device does "
			       "not have the PDO layout, which is expected for its "
			       "product code.",
			       sid, mapped_in, mapped_out, expected_in, expected_out);

			return false;
		}

		MSG ("slave[%d] mapped %u input and %u output byte(s), as expected",
		     sid, mapped_in, mapped_out);
	}

	ecx_statecheck (&self->ecx_context, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE);

	// verify the PO2SO callback executed completely
	for (sid = 1; sid <= self->ecx_context.slavecount; sid++)
	{
		jsd_slave_config_t *config = &self->slave_configs[sid];

		if (   config->configuration_active
		    && !config->PO2SO_success)
		{
			ERROR ("PO2SO callback did not execute successfully, "
			       "failed on slave %d", sid);

			return false;
		}
	}

	// auto-configure Distributed Clock capable slaves
	ecx_configdc (&self->ecx_context);

	// Register the slaves with a mailbox at the cyclic mailbox handler of
	// SOEM, which is pumped by jsd_read(). Without this, an SDO transfer
	// from another task would poll the slave mailbox itself and could stall
	// the process data cycle.
	for (sid = 1; sid <= self->ecx_context.slavecount; sid++)
	{
		if (self->ecx_context.slavelist[sid].mbx_proto & ECT_MBXPROT_COE)
		{
			ecx_slavembxcyclic (&self->ecx_context, sid);
		}
	}

	ecx_readstate (&self->ecx_context);

	self->expected_wkc = (self->ecx_context.grouplist[0].outputsWKC * 2)
			   +  self->ecx_context.grouplist[0].inputsWKC;

	self->last_wkc = -1;	// -1 is returned on first read

	MSG_DEBUG ("Calculated workcounter %d", self->expected_wkc);

	// Request the OPERATIONAL state. Process data must be exchanged
	// continuously during this transition.
	self->ecx_context.slavelist[0].state = EC_STATE_OPERATIONAL;

	ecx_send_processdata (&self->ecx_context);
	ecx_receive_processdata (&self->ecx_context, timeout_us);

	ecx_writestate (&self->ecx_context, 0);

	bool operational = false;

	for (int attempt = 1;
	        attempt <= JSD_PO2OP_MAX_ATTEMPTS && !operational;
	        attempt++)
	{
		for (int cycle = 0; cycle < JSD_PO2OP_CYCLES_PER_ATTEMPT; cycle++)
		{
			ecx_send_processdata (&self->ecx_context);
			ecx_receive_processdata (&self->ecx_context, timeout_us);

			if (ecx_statecheck (&self->ecx_context, 0, EC_STATE_OPERATIONAL,
					    JSD_PO2OP_STATECHECK_TIMEOUT)
			    == EC_STATE_OPERATIONAL)
			{
				operational = true;

				break;
			}

			if (CScheduler::IsActive ())
			{
				CScheduler::Get ()->usSleep (timeout_us);
			}
		}

		if (!operational)
		{
			WARNING ("Did not reach EC_STATE_OPERATIONAL, "
				 "failed attempt %d of %d", attempt,
				 JSD_PO2OP_MAX_ATTEMPTS);

			jsd_inspect_context (self);

			// request the state again for the next attempt
			self->ecx_context.slavelist[0].state = EC_STATE_OPERATIONAL;
			ecx_writestate (&self->ecx_context, 0);
		}
	}

	if (!operational)
	{
		ERROR ("Max number of attempts to transition to OPERATIONAL exceeded");

		return false;
	}

	// initialize the error queues, which are used between the tasks
	for (sid = 1; sid <= self->ecx_context.slavecount; sid++)
	{
		CString Name;
		Name.Format ("Slave %d error cirq", sid);

		jsd_error_cirq_init (&self->slave_errors[sid], (const char *) Name);
	}

	// initialize the SDO request/response queues and start the SDO handler
	jsd_sdo_req_cirq_init (&self->jsd_sdo_req_cirq, "Request Queue");
	jsd_sdo_req_cirq_init (&self->jsd_sdo_res_cirq, "Response Queue");

	if (!jsd_sdo_start_handler (self))
	{
		ERROR ("Failed to start the SDO handler task");

		return false;
	}

	self->init_complete = true;

	SUCCESS ("JSD is Operational");

	return true;
}

bool jsd_all_slaves_operational (jsd_t *self)
{
	assert (self);

	bool all_slaves_operational = true;
	uint8_t currentgroup = 0;	// only 1 rate group in JSD currently

	for (int slave = 1; slave <= self->ecx_context.slavecount; slave++)
	{
		if (self->ecx_context.slavelist[slave].group != currentgroup)
		{
			continue;
		}

		ecx_statecheck (&self->ecx_context, slave, EC_STATE_OPERATIONAL,
				EC_TIMEOUTRET);

		if (self->ecx_context.slavelist[slave].state != EC_STATE_OPERATIONAL)
		{
			all_slaves_operational = false;

			ERROR ("slave[%d] is in state 0x%X", slave,
			       self->ecx_context.slavelist[slave].state);
		}
	}

	return all_slaves_operational;
}

void jsd_inspect_context (jsd_t *self)
{
	assert (self);

	if (jsd_get_device_state (self, 0) != EC_STATE_OPERATIONAL)
	{
		ERROR ("JSD bus is not OPERATIONAL.");
	}

	if (jsd_all_slaves_operational (self))
	{
		MSG ("All slaves were operational at time of working counter fault.");

		return;
	}

	MSG ("Some slaves were not operational.");

	if (!ecx_iserror (&self->ecx_context))
	{
		MSG ("Despite some slaves not being operational, "
		     "an ECAT error was not experienced.");

		return;
	}

	unsigned i = 0;
	while (   ecx_iserror (&self->ecx_context)
	       && i < JSD_ELIST_MAX_READS)
	{
		MSG ("%s", ecx_elist2string (&self->ecx_context));

		i++;
	}

	if (i == JSD_ELIST_MAX_READS)
	{
		MSG ("Maximum number of reads from error list experienced.");
	}
}

void jsd_read (jsd_t *self, int timeout_us)
{
	assert (self);

	self->wkc = ecx_receive_processdata (&self->ecx_context, timeout_us);

	if (   self->wkc != self->expected_wkc
	    && self->last_wkc != self->wkc)
	{
		WARNING ("ecx_receive_processdata returning bad wkc: %d (expected: %d)",
			 self->wkc, self->expected_wkc);
	}

	if (   self->last_wkc != self->expected_wkc
	    && self->wkc == self->expected_wkc
	    && self->last_wkc != -1)
	{
		MSG ("ecx_receive_processdata is no longer reading bad wkc");
	}

	self->last_wkc = self->wkc;

	// Service the mailboxes of the slaves, which are registered at the
	// cyclic mailbox handler. This makes SDO transfers from other tasks
	// possible, without blocking the process data cycle here.
	ecx_mbxhandler (&self->ecx_context, 0, JSD_MBX_HANDLER_LIMIT);

	if (   self->enable_autorecovery
	    || self->attempt_manual_recovery)
	{
		jsd_ecatcheck (self);

		self->attempt_manual_recovery = 0;
	}
}

void jsd_write (jsd_t *self)
{
	assert (self);

	int transmitted = ecx_send_processdata (&self->ecx_context);

	static int last_transmitted = 1;
	if (   transmitted <= 0
	    && last_transmitted != transmitted)
	{
		WARNING ("ecx_send_processdata is not transmitting");
	}

	if (   last_transmitted <= 0
	    && transmitted > 0)
	{
		MSG ("ecx_send_processdata has resumed transmission");
	}

	last_transmitted = transmitted;
}

void jsd_free (jsd_t *self)
{
	if (self == 0)
	{
		return;
	}

	if (self->init_complete)
	{
		jsd_sdo_stop_handler (self);
	}

	MSG_DEBUG ("Closing the net device...");

	ecx_close (&self->ecx_context);

	free (self);
}

ec_state jsd_get_device_state (jsd_t *self, uint16_t slave_id)
{
	assert (self);

	return (ec_state) self->ecx_context.slavelist[slave_id].state;
}

void jsd_set_manual_recovery (jsd_t *self)
{
	assert (self);

	self->attempt_manual_recovery = 1;
}

/****************************************************
 * Private functions
 ****************************************************/

char *jsd_ec_state_to_string (ec_state state)
{
	// Note: the original implementation returns strdup() for unknown states,
	// which cannot be freed by the caller consistently. A static string is
	// returned here instead.
	switch (state)
	{
	case EC_STATE_NONE:		return (char *) "EC_STATE_NONE";
	case EC_STATE_INIT:		return (char *) "EC_STATE_INIT";
	case EC_STATE_PRE_OP:		return (char *) "EC_STATE_PRE_OP";
	case EC_STATE_BOOT:		return (char *) "EC_STATE_BOOT";
	case EC_STATE_SAFE_OP:		return (char *) "EC_STATE_SAFE_OP";
	case EC_STATE_OPERATIONAL:	return (char *) "EC_STATE_OPERATIONAL";
	case EC_STATE_ACK:		return (char *) "EC_STATE_ACK/ERROR";
	default:			break;
	}

	return (char *) "Bad EC_STATE";
}

const char *jsd_driver_type_to_string (jsd_driver_type_t driver_type)
{
	switch (driver_type)
	{
	case JSD_DRIVER_TYPE_EGD:		return "JSD_DRIVER_TYPE_EGD";
	case JSD_DRIVER_TYPE_EPD_NOMINAL:	return "JSD_DRIVER_TYPE_EPD_NOMINAL";
	case JSD_DRIVER_TYPE_EPD_SIL:		return "JSD_DRIVER_TYPE_EPD_SIL";
	default:				break;
	}

	return "Unsupported Driver Type";
}

bool jsd_init_all_devices (jsd_t *self)
{
	assert (self);

	uint16_t num_found_slaves = self->ecx_context.slavecount;
	assert (num_found_slaves < EC_MAXSLAVE);

	MSG ("Configuring Slaves:");

	for (uint16_t slave_idx = 1; slave_idx <= num_found_slaves; slave_idx++)
	{
		ec_slavet *slave = &self->ecx_context.slavelist[slave_idx];

		if (!self->slave_configs[slave_idx].configuration_active)
		{
			MSG ("\tslave[%u] %s - Ignored", slave_idx, slave->name);

			continue;
		}

		jsd_driver_type_t driver_type =
			self->slave_configs[slave_idx].driver_type;

		if (!jsd_driver_is_compatible_with_product_code (driver_type,
								 slave->eep_id))
		{
			ERROR ("User-specified driver type (%s) is incompatible with "
			       "the device's product code (0x%X).",
			       jsd_driver_type_to_string (driver_type), slave->eep_id);

			return false;
		}

		if (!jsd_init_single_device (self, slave_idx))
		{
			ERROR ("\tBad Cfg slave[%u] Active Device configuration mismatch!",
			       slave_idx);

			return false;
		}

		SUCCESS ("\tslave[%u] %s - Configured", slave_idx,
			 jsd_driver_type_to_string (driver_type));
	}

	return true;
}

bool jsd_driver_is_compatible_with_product_code (jsd_driver_type_t driver_type,
						 uint32_t product_code)
{
	// Only the Elmo drivers are built for Circle. Add the other JSD device
	// drivers to the library and to this function, if you need them.
	switch (driver_type)
	{
	case JSD_DRIVER_TYPE_EGD:
		return jsd_egd_product_code_is_compatible (product_code);

	case JSD_DRIVER_TYPE_EPD_NOMINAL:
		return jsd_epd_nominal_product_code_is_compatible (product_code);

	default:
		break;
	}

	ERROR ("Unsupported driver type (%d)", driver_type);

	return false;
}

bool jsd_init_single_device (jsd_t *self, uint16_t slave_id)
{
	assert (self);

	jsd_driver_type_t driver_type = self->slave_configs[slave_id].driver_type;

	switch (driver_type)
	{
	case JSD_DRIVER_TYPE_EGD:
		return jsd_egd_init (self, slave_id);

	case JSD_DRIVER_TYPE_EPD_NOMINAL:
		return jsd_epd_nominal_init (self, slave_id);

	default:
		break;
	}

	ERROR ("Unsupported driver type: %u", driver_type);

	return false;
}

void jsd_ecatcheck (jsd_t *self)
{
	assert (self);

	uint8_t currentgroup = 0;	// only 1 rate group in JSD currently

	ec_state bus_state = jsd_get_device_state (self, 0);

	if (!(   (   bus_state == EC_STATE_OPERATIONAL
		  && self->wkc < self->expected_wkc)
	      || self->ecx_context.grouplist[currentgroup].docheckstate))
	{
		return;
	}

	// one or more slaves are not responding
	self->ecx_context.grouplist[currentgroup].docheckstate = FALSE;

	ecx_readstate (&self->ecx_context);

	for (int slave = 1; slave <= self->ecx_context.slavecount; slave++)
	{
		ec_slavet *slavep = &self->ecx_context.slavelist[slave];

		if (   slavep->group == currentgroup
		    && slavep->state != EC_STATE_OPERATIONAL)
		{
			self->ecx_context.grouplist[currentgroup].docheckstate = TRUE;

			if (slavep->state == EC_STATE_SAFE_OP + EC_STATE_ERROR)
			{
				MSG_DEBUG ("slave[%d] is in SAFE_OP + ERROR, "
					   "attempting ack.", slave);

				slavep->state = EC_STATE_SAFE_OP + EC_STATE_ACK;
				ecx_writestate (&self->ecx_context, slave);
			}
			else if (slavep->state == EC_STATE_SAFE_OP)
			{
				MSG_DEBUG ("slave[%d] is in SAFE_OP, "
					   "changing to OPERATIONAL.", slave);

				slavep->state = EC_STATE_OPERATIONAL;
				ecx_writestate (&self->ecx_context, slave);
			}
			else if (slavep->state > EC_STATE_NONE)
			{
				if (ecx_reconfig_slave (&self->ecx_context, slave,
							EC_TIMEOUTRET3))
				{
					slavep->islost = FALSE;

					MSG ("slave[%d] was reconfigured", slave);
				}
			}
			else if (!slavep->islost)
			{
				ecx_statecheck (&self->ecx_context, slave,
						EC_STATE_OPERATIONAL, EC_TIMEOUTRET);

				if (slavep->state == EC_STATE_NONE)
				{
					slavep->islost = TRUE;

					ERROR ("slave[%d] is lost", slave);
				}
			}
		}

		if (slavep->islost)
		{
			if (slavep->state == EC_STATE_NONE)
			{
				if (ecx_recover_slave (&self->ecx_context, slave,
						       EC_TIMEOUTRET3))
				{
					slavep->islost = FALSE;

					MSG ("slave[%d] recovered", slave);
				}
			}
			else
			{
				slavep->islost = FALSE;

				MSG ("slave %d found", slave);
			}
		}
	}

	if (!self->ecx_context.grouplist[currentgroup].docheckstate)
	{
		SUCCESS ("all slaves resumed OPERATIONAL.");
	}
}
