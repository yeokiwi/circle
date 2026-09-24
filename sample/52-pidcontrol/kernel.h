//
// kernel.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
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
#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/types.h>
#include <pid/pidcontroller.h>
#include <pid/pidautotuner.h>
#include "heaterplant.h"

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);

	TShutdownMode Run (void);

private:
	struct TPhase
	{
		const char *pName;
		float	fDuration;		// seconds
		float	fSetpoint;		// degrees C
		float	fDisturbance;		// additional heat loss (K)
		boolean	bManual;		// controller in manual mode?
		float	fManualOutput;		// heater power in manual mode (%)
	};

	void AutoTune (void);
	void RunPhase (const TPhase *pPhase);

	void WaitForNextSample (void);

private:
	// do not change this order
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CScreenDevice		m_Screen;
	CSerialDevice		m_Serial;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CTimer			m_Timer;
	CLogger			m_Logger;

	CHeaterPlant		m_Plant;
	CPIDController		m_PID;
	CPIDAutoTuner		m_AutoTuner;

	u64			m_nNextSampleTicks;
	float			m_fTime;		// seconds since start of control
	unsigned		m_nOverruns;

	static const TPhase	s_Phases[];
};

#endif
