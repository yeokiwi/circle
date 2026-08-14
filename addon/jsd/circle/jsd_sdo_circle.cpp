//
// jsd_sdo_circle.cpp
//
// SDO layer of the Circle port of JSD
//
// This file replaces src/jsd_sdo.c of JSD. The request and response queues and
// the blocking SDO transfers work like in the original implementation. The
// background thread, which services the asynchronous requests and collects the
// emergency messages, is a Circle task here.
//
// Because the slaves with a mailbox are registered at the cyclic mailbox
// handler of SOEM 2.0 (see jsd_init()), which is pumped once per process data
// cycle from jsd_read(), an SDO transfer from this task does not stall the
// process data cycle of the application.
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
#include <jsd/jsd_sdo.h>
#include <jsd/jsd_circle.h>
#include <jsd/jsd_error_cirq.h>
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/util.h>
#include <assert.h>
#include <pthread.h>

// maximum number of errors, which are handled in one pass
#define SDO_MAX_ERRORS_PER_LOOP		32

// stack size of the SDO handler task
#define SDO_HANDLER_STACK_SIZE		(32 * 1024)

///////////////////  ASYNC SDO /////////////////////////////

void jsd_sdo_req_cirq_init (jsd_sdo_req_cirq_t *self, const char *name)
{
	assert (self);

	self->r = 0;
	self->w = 0;
	self->mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;

	strncpy (self->name, name, JSD_NAME_LEN-1);
	self->name[JSD_NAME_LEN-1] = '\0';
}

// non-locking
static jsd_sdo_req_t queue_pop (jsd_sdo_req_cirq_t *self)
{
	jsd_sdo_req_t new_req = self->buffer[self->r % JSD_SDO_REQ_CIRQ_LEN];
	self->r++;

	return new_req;
}

// non-locking
static bool queue_push (jsd_sdo_req_cirq_t *self, jsd_sdo_req_t req)
{
	bool status = true;

	self->buffer[self->w % JSD_SDO_REQ_CIRQ_LEN] = req;
	self->w++;

	if ((uint16_t) (self->w - self->r) > JSD_SDO_REQ_CIRQ_LEN)
	{
		self->r++;

		WARNING ("[%s] is overflowing: r=%u w=%u", self->name, self->r, self->w);

		status = false;
	}

	return status;
}

static bool queue_is_empty (jsd_sdo_req_cirq_t *self)
{
	return self->r == self->w;
}

jsd_sdo_req_t jsd_sdo_req_cirq_pop (jsd_sdo_req_cirq_t *self)
{
	assert (self);

	jsd_sdo_req_t new_req;
	memset (&new_req, 0, sizeof new_req);

	if (jsd_sdo_req_cirq_is_empty (self))
	{
		WARNING ("Popping [%s] when empty", self->name);

		return new_req;
	}

	pthread_mutex_lock (&self->mutex);
	new_req = queue_pop (self);
	pthread_mutex_unlock (&self->mutex);

	return new_req;
}

bool jsd_sdo_req_cirq_push (jsd_sdo_req_cirq_t *self, jsd_sdo_req_t req)
{
	assert (self);

	pthread_mutex_lock (&self->mutex);
	bool status = queue_push (self, req);
	pthread_mutex_unlock (&self->mutex);

	return status;
}

bool jsd_sdo_req_cirq_is_empty (jsd_sdo_req_cirq_t *self)
{
	assert (self);

	pthread_mutex_lock (&self->mutex);
	bool val = queue_is_empty (self);
	pthread_mutex_unlock (&self->mutex);

	return val;
}

int jsd_sdo_data_type_size (jsd_sdo_data_type_t type)
{
	switch (type)
	{
	case JSD_SDO_DATA_I8:
	case JSD_SDO_DATA_U8:
		return 1;

	case JSD_SDO_DATA_I16:
	case JSD_SDO_DATA_U16:
		return 2;

	case JSD_SDO_DATA_I32:
	case JSD_SDO_DATA_FLOAT:
	case JSD_SDO_DATA_U32:
		return 4;

	case JSD_SDO_DATA_I64:
	case JSD_SDO_DATA_DOUBLE:
	case JSD_SDO_DATA_U64:
		return 8;

	default:
		break;
	}

	WARNING ("Unknown jsd_sdo_data_type_t: %d", type);

	return 0;
}

jsd_sdo_req_t jsd_sdo_populate_request (uint16_t slave_id, uint16_t index,
					uint8_t subindex,
					jsd_sdo_data_type_t data_type, void *data,
					jsd_sdo_req_type_t request_type,
					uint16_t app_id)
{
	jsd_sdo_req_t req;
	memset (&req, 0, sizeof req);

	req.slave_id     = slave_id;
	req.sdo_index    = index;
	req.sdo_subindex = subindex;
	req.data_type    = data_type;
	req.app_id       = app_id;
	req.request_type = request_type;

	if (JSD_SDO_REQ_TYPE_WRITE == request_type)
	{
		if (data == NULL)
		{
			WARNING ("Slave[%d] Invalid SDO-Write data for async request "
				 "(0x%X:%d)", slave_id, index, subindex);

			req.request_type = JSD_SDO_REQ_TYPE_INVALID;
		}
		else
		{
			memcpy (&req.data, data, jsd_sdo_data_type_size (data_type));
		}
	}

	req.success = false;
	req.wkc = 0;

	return req;
}

bool jsd_sdo_push_async_request (jsd_t *self, jsd_sdo_req_t *request)
{
	assert (self);
	assert (request);

	if (request->request_type == JSD_SDO_REQ_TYPE_INVALID)
	{
		WARNING ("Slave[%d] invalid SDO request on 0x%X:%d app_id: (%u)",
			 request->slave_id, request->sdo_index,
			 request->sdo_subindex, request->app_id);

		return false;
	}

	jsd_sdo_req_cirq_push (&self->jsd_sdo_req_cirq, *request);

	self->slave_states[request->slave_id].num_async_sdo_requests++;

	return true;
}

bool jsd_sdo_set_param_async (jsd_t *self, uint16_t slave_id, uint16_t index,
			      uint8_t subindex, jsd_sdo_data_type_t data_type,
			      void *data, uint16_t app_id)
{
	jsd_sdo_req_t request = jsd_sdo_populate_request (slave_id, index, subindex,
							  data_type, data,
							  JSD_SDO_REQ_TYPE_WRITE,
							  app_id);

	return jsd_sdo_push_async_request (self, &request);
}

bool jsd_sdo_get_param_async (jsd_t *self, uint16_t slave_id, uint16_t index,
			      uint8_t subindex, jsd_sdo_data_type_t data_type,
			      uint16_t app_id)
{
	jsd_sdo_req_t request = jsd_sdo_populate_request (slave_id, index, subindex,
							  data_type, NULL,
							  JSD_SDO_REQ_TYPE_READ,
							  app_id);

	return jsd_sdo_push_async_request (self, &request);
}

void jsd_sdo_signal_emcy_check (jsd_t *self)
{
	assert (self);

	self->raise_sdo_thread_cond = true;
}

bool jsd_sdo_pop_response_queue (jsd_t *self, jsd_sdo_req_t *res)
{
	assert (self);
	assert (res);

	bool retval = false;
	jsd_sdo_req_cirq_t *sdo_queue = &self->jsd_sdo_res_cirq;

	pthread_mutex_lock (&sdo_queue->mutex);

	if (!queue_is_empty (sdo_queue))
	{
		jsd_sdo_req_t popped_res = queue_pop (sdo_queue);
		memcpy (res, &popped_res, sizeof popped_res);

		retval = true;
	}

	pthread_mutex_unlock (&sdo_queue->mutex);

	return retval;
}

///////////////////  BLOCKING SDO /////////////////////////////

bool jsd_sdo_set_param_blocking (ecx_contextt *ecx_context, uint16_t slave_id,
				 uint16_t index, uint8_t subindex,
				 jsd_sdo_data_type_t data_type, void *param_in)
{
	assert (ecx_context);

	int param_size = jsd_sdo_data_type_size (data_type);

	int wkc = ecx_SDOwrite (ecx_context, slave_id, index, subindex, FALSE,
				param_size, param_in, JSD_SDO_TIMEOUT);
	if (wkc == 0)
	{
		WARNING ("Slave[%d] Failed to write SDO: 0x%X:%d", slave_id, index,
			 subindex);

		return false;
	}

	return true;
}

bool jsd_sdo_get_param_blocking (ecx_contextt *ecx_context, uint16_t slave_id,
				 uint16_t index, uint8_t subindex,
				 jsd_sdo_data_type_t data_type, void *param_out)
{
	assert (ecx_context);

	int param_size = jsd_sdo_data_type_size (data_type);

	int wkc = ecx_SDOread (ecx_context, slave_id, index, subindex, FALSE,
			       &param_size, param_out, JSD_SDO_TIMEOUT);
	if (wkc == 0)
	{
		WARNING ("Slave[%d] Failed to read SDO: 0x%X:%d", slave_id, index,
			 subindex);

		return false;
	}

	return true;
}

bool jsd_sdo_set_ca_param_blocking (ecx_contextt *ecx_context, uint16_t slave_id,
				    uint16_t index, uint8_t subindex,
				    int param_size, void *param_in)
{
	assert (ecx_context);

	int wkc = ecx_SDOwrite (ecx_context, slave_id, index, subindex, TRUE,
				param_size, param_in, JSD_SDO_TIMEOUT);
	if (wkc == 0)
	{
		MSG_DEBUG ("Slave[%d] Failed to write SDO: 0x%X:%d", slave_id, index,
			   subindex);

		return false;
	}

	return true;
}

bool jsd_sdo_get_ca_param_blocking (ecx_contextt *ecx_context, uint16_t slave_id,
				    uint16_t index, uint8_t subindex,
				    int *param_size_in_out, void *param_out)
{
	assert (ecx_context);

	int wkc = ecx_SDOread (ecx_context, slave_id, index, subindex, TRUE,
			       param_size_in_out, param_out, JSD_SDO_TIMEOUT);
	if (wkc == 0)
	{
		WARNING ("Slave[%d] Failed to read SDO: 0x%X:%d", slave_id, index,
			 subindex);

		return false;
	}

	return true;
}

///////////////////  SDO HANDLER TASK /////////////////////////////

/// \brief Services one SDO request
static void HandleRequest (jsd_t *self, jsd_sdo_req_t *req)
{
	int param_size = jsd_sdo_data_type_size (req->data_type);

	switch (req->request_type)
	{
	case JSD_SDO_REQ_TYPE_WRITE:
		req->wkc = ecx_SDOwrite (&self->ecx_context, req->slave_id,
					 req->sdo_index, req->sdo_subindex, FALSE,
					 param_size, (void *) &req->data,
					 JSD_SDO_TIMEOUT);
		req->success = (req->wkc == 1);

		if (!req->success)
		{
			WARNING ("Slave[%d] Bad SDO Write on 0x%X:%d app_id: (%u)",
				 req->slave_id, req->sdo_index, req->sdo_subindex,
				 req->app_id);
		}
		break;

	case JSD_SDO_REQ_TYPE_READ:
		req->wkc = ecx_SDOread (&self->ecx_context, req->slave_id,
					req->sdo_index, req->sdo_subindex, FALSE,
					&param_size, (void *) &req->data,
					JSD_SDO_TIMEOUT);
		req->success = (req->wkc == 1);

		if (!req->success)
		{
			WARNING ("Slave[%d] Bad SDO read on 0x%X:%d app_id: (%u)",
				 req->slave_id, req->sdo_index, req->sdo_subindex,
				 req->app_id);
		}
		break;

	case JSD_SDO_REQ_TYPE_INVALID:
	default:
		req->success = false;

		WARNING ("Slave[%d] invalid SDO request on 0x%X:%d app_id: (%u)",
			 req->slave_id, req->sdo_index, req->sdo_subindex,
			 req->app_id);
		break;
	}
}

/// \brief Moves the emergency messages from the SOEM error list to the error
///	   queue of the respective slave
static void HandleErrors (jsd_t *self)
{
	unsigned handled_errors = 0;

	while (   ecx_iserror (&self->ecx_context)
	       && handled_errors < SDO_MAX_ERRORS_PER_LOOP)
	{
		ec_errort err;
		if (!ecx_poperror (&self->ecx_context, &err))
		{
			break;
		}

		handled_errors++;

		ERROR ("%s", ecx_err2string (err));

		if (err.Etype == EC_ERR_TYPE_EMERGENCY)
		{
			if (   err.Slave > 0
			    && err.Slave < EC_MAXSLAVE)
			{
				jsd_error_cirq_push (&self->slave_errors[err.Slave], err);
			}
		}
	}
}

class CJSDSDOHandler : public CTask
{
public:
	CJSDSDOHandler (jsd_t *pJSD)
	:	CTask (SDO_HANDLER_STACK_SIZE),
		m_pJSD (pJSD)
	{
		SetName ("jsdsdo");
	}

	void Run (void) override
	{
		assert (m_pJSD != 0);

		while (!m_pJSD->sdo_join_flag)
		{
			m_pJSD->raise_sdo_thread_cond = false;

			// service the queued requests
			while (   !jsd_sdo_req_cirq_is_empty (&m_pJSD->jsd_sdo_req_cirq)
			       && !m_pJSD->sdo_join_flag)
			{
				jsd_sdo_req_t req =
					jsd_sdo_req_cirq_pop (&m_pJSD->jsd_sdo_req_cirq);

				HandleRequest (m_pJSD, &req);

				jsd_sdo_req_cirq_push (&m_pJSD->jsd_sdo_res_cirq, req);
			}

			// collect the emergency messages of the slaves
			HandleErrors (m_pJSD);

			CScheduler::Get ()->MsSleep (JSD_SDO_HANDLER_INTERVAL_MS);
		}
	}

private:
	jsd_t *m_pJSD;
};

bool jsd_sdo_start_handler (jsd_t *self)
{
	assert (self);

	if (!CScheduler::IsActive ())
	{
		ERROR ("The Circle scheduler is required to use JSD");

		return false;
	}

	self->sdo_join_flag = false;

	CTask *pTask = new CJSDSDOHandler (self);
	if (pTask == 0)
	{
		return false;
	}

	self->sdo_thread = pTask;

	return true;
}

void jsd_sdo_stop_handler (jsd_t *self)
{
	assert (self);

	if (self->sdo_thread == 0)
	{
		return;
	}

	self->sdo_join_flag = true;

	MSG ("Waiting for the SDO handler task to terminate...");

	((CTask *) self->sdo_thread)->WaitForTermination ();

	self->sdo_thread = 0;
}
