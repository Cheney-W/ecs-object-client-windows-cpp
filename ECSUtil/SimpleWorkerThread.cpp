//
// Copyright (C) 1994 - 2012 EMC Corporation
// All rights reserved.
//


#include "stdafx.h"

using namespace std;

#include "generic_defs.h"
#include "ProcessEvent.h"
#include "SimpleWorkerThread.h"
#include "Logging.h"

CCriticalSection *CSimpleWorkerThread::pcsGlobalThreadSet;
set<CSimpleWorkerThread *> *CSimpleWorkerThread::pGlobalThreadSetActive;
set<CSimpleWorkerThread *> *CSimpleWorkerThread::pGlobalThreadSet;

CSimpleWorkerThread::CSimpleWorkerThread()
	: pThread(NULL)
	, dwThreadID(0)
	, hThread(NULL)
	, dwWaitTime(INFINITE)
	, bRunning(false)
	, bTerminate(false)
	, bAlertable(false)
	, dwEventRet(0)
{
	ZeroFT(ftEndThreadTime);
	// keep a list of all current threads
	{
		if (pcsGlobalThreadSet == NULL)
		{
			CCriticalSection *pcsGlobalThreadTemp = new CCriticalSection;			//lint !e1732 !e1733: (Info -- new in constructor for class 'CSimpleWorkerThread' which has no assignment operator)
			if (InterlockedCompareExchangePointer((void **)&pcsGlobalThreadSet, pcsGlobalThreadTemp, NULL) != NULL)
				delete pcsGlobalThreadTemp;
		}
		ASSERT(pcsGlobalThreadSet != NULL);
		CSingleLock lockGlobalList(pcsGlobalThreadSet, true);
		if (pGlobalThreadSet == NULL)
			pGlobalThreadSet = new set<CSimpleWorkerThread *>;			//lint !e1732 !e1733	
		if (pGlobalThreadSetActive == NULL)
			pGlobalThreadSetActive = new set<CSimpleWorkerThread *>;			//lint !e1732 !e1733	
		(void)pGlobalThreadSet->insert(this);
	}
}

CSimpleWorkerThread::~CSimpleWorkerThread()
{
	KillThreadWait();
	if (hThread != NULL)
	{
		if (!CloseHandle(hThread))				// allow the thread to terminate
		{
#ifdef unused
			if (LgtoavlibData.dwMSG_FILE_CLOSE_WARNING != 0)
			{
				DWORD dwError = GetLastError();
				OctopusMsgLog(LOG_POS, LgtoavlibData.dwMSG_FILE_CLOSE_WARNING, dwError, TEXT(""), (LPCTSTR)GetNTErrorText(dwError));	//lint !e1551: (Warning -- Function may throw exception '...' in destructor 'CSimpleWorkerThread::~CSimpleWorkerThread(void)')
			}
#endif
		}
	}
	if (pcsGlobalThreadSet && pGlobalThreadSet)
	{
		try
		{
			CSingleLock csGlobalList(pcsGlobalThreadSet, true);
			(void)pGlobalThreadSet->erase(this);
		}
		catch (...)
		{}
	}
	hThread = NULL;
	pThread = NULL;
}

bool CSimpleWorkerThread::CreateThread(
	UINT nStackSize,
	LPSECURITY_ATTRIBUTES lpSecurityAttrs,
	int nPriority)
{
	{
		CSingleLock lockWorkEvent(&Events.csWorkEvent, true);
		if (Events.m_pWorkEvent != NULL)
			delete Events.m_pWorkEvent;
		Events.m_pWorkEvent = new CEvent();
		ASSERT(Events.m_pWorkEvent != NULL);
		if (hThread != NULL)
		{
			if (!CloseHandle(hThread))				// allow the thread to terminate
			{
#ifdef unused
				if (LgtoavlibData.dwMSG_FILE_CLOSE_WARNING != 0)
				{
					DWORD dwError = GetLastError();
					OctopusMsgLog(LOG_POS, LgtoavlibData.dwMSG_FILE_CLOSE_WARNING, dwError, TEXT(""), (LPCTSTR)GetNTErrorText(dwError));
				}
#endif
			}
			hThread = NULL;
			pThread = NULL;
		}
	}
	bTerminate = false;
	// create the thread suspended because there is a race condition:
	// the thread may get going before the pThread field gets initialized
	pThread = AfxBeginThread(ThreadProc, this, nPriority, nStackSize, CREATE_SUSPENDED, lpSecurityAttrs);
	if (pThread != NULL)
	{
		CSingleLock lock(&Events.csWorkEvent, true);
		dwThreadID = pThread->m_nThreadID;
		hThread = OpenThread(SYNCHRONIZE | THREAD_QUERY_INFORMATION, FALSE, dwThreadID);
		bRunning = true;
		(void)pThread->ResumeThread();
	}
	return pThread != NULL;
}

void CSimpleWorkerThread::StartWork()
{
	CSingleLock lock(&Events.csWorkEvent, true);

	if (Events.m_pWorkEvent != NULL)
		(void)Events.m_pWorkEvent->SetEvent();
}

UINT CSimpleWorkerThread::ThreadProc(LPVOID pParam)
{
	CSimpleWorkerThread *pSimpleThread = (CSimpleWorkerThread *)pParam;
	HANDLE EventArray[MAXIMUM_WAIT_OBJECTS];
	DWORD iEvent;

	// keep a list of all current threads
	{
		CSingleLock csGlobalList(pcsGlobalThreadSet, true);
		pair<set<CSimpleWorkerThread *>::iterator,bool> ret = pGlobalThreadSetActive->insert(pSimpleThread);
		ASSERT(ret.second);
	}
	if (!pSimpleThread->InitInstance())
		pSimpleThread->bTerminate = true;
	while (!pSimpleThread->bTerminate)
	{
		// create handle list
		iEvent = 0;
		for (UINT i=0 ; (i<pSimpleThread->EventList.size()) && (i < (MAXIMUM_WAIT_OBJECTS - 1)) ; i++)
			EventArray[iEvent++] = pSimpleThread->EventList[i]->m_hObject;
		EventArray[iEvent++] = pSimpleThread->Events.m_pWorkEvent->m_hObject;		// the first is the default event
		pSimpleThread->dwEventRet = WaitForMultipleObjectsEx(iEvent, EventArray, false, pSimpleThread->dwWaitTime, pSimpleThread->bAlertable);
		if (pSimpleThread->dwEventRet == WAIT_FAILED)
		{
#ifdef unused
			DWORD dwWaitError = GetLastError();
			if (pSimpleThread->WaitFailed(dwWaitError))
				if (LgtoavlibData.multithrd_data.dwMSG_WAIT_FOR_MULTIPLE_EVENT_ERROR != 0)
					OctopusMsgLog(LOG_POS, LgtoavlibData.multithrd_data.dwMSG_WAIT_FOR_MULTIPLE_EVENT_ERROR, dwWaitError, NTLT(dwWaitError));
#endif
		}
		else
			pSimpleThread->DoWork();
	}
	pSimpleThread->ExitInstance();
	{
		CSingleLock csGlobalList(pcsGlobalThreadSet, true);
		(void)pGlobalThreadSetActive->erase(pSimpleThread);
	}
	{
		CSingleLock lock(&pSimpleThread->Events.csWorkEvent, true);
		if (pSimpleThread->Events.m_pWorkEvent != NULL)
		{
			delete pSimpleThread->Events.m_pWorkEvent;
			pSimpleThread->Events.m_pWorkEvent = NULL;
		}
		pSimpleThread->pThread = NULL;
		if (pSimpleThread->hThread != NULL)
		{
			if (!CloseHandle(pSimpleThread->hThread))				// allow the thread to terminate
			{
#ifdef unused
				if (LgtoavlibData.dwMSG_FILE_CLOSE_WARNING != 0)
				{
					DWORD dwError = GetLastError();
					OctopusMsgLog(LOG_POS, LgtoavlibData.dwMSG_FILE_CLOSE_WARNING, dwError, TEXT(""), (LPCTSTR)GetNTErrorText(dwError));
				}
#endif
			}
			pSimpleThread->hThread = NULL;
			pSimpleThread->dwThreadID = 0;
		}
	pSimpleThread->bRunning = false;
	}
	return 0;
}

void CSimpleWorkerThread::KillThread() throw()
{
	CSingleLock lock(&Events.csWorkEvent, true);

	GetSystemTimeAsFileTime(&ftEndThreadTime);
	bTerminate = true;
	if (Events.m_pWorkEvent != NULL)
		(void)Events.m_pWorkEvent->SetEvent();
}

void CSimpleWorkerThread::KillThreadWait() throw()
{
	if ((pThread == NULL) || (dwThreadID == 0))
		return;
	DWORD dwRet;
	CThreadEvent ThreadEvent;
	DWORD dwEventError = ThreadEvent.Create(dwThreadID);
	if (dwEventError != ERROR_SUCCESS)
	{
		DEBUGF(L"CSimpleWorkerThread::KillThreadWait ThreadEvent error: cur:%d kill:%d Error:%d", GetCurrentThreadId(), dwThreadID, dwEventError);
	}
	KillThread();
	CSingleLock lockKillThread(&Events.csKillThread, true);
	if (Events.m_pWorkEvent == NULL)
		return;
	while (IfActive())
	{
		// Start up the other thread so it can complete.
		// When it does, it will set the exit event and the object can be 
		// destructed.
		// wait for the thread to terminate
		if (dwEventError == ERROR_SUCCESS)
		{
			dwRet = WaitForSingleObject(ThreadEvent.m_hObject, 100);
			if (dwRet == WAIT_TIMEOUT)
				continue;
			if (dwRet == WAIT_FAILED)
				break;
		}
		else
			Sleep(100);
	}
	{
		CSingleLock lockWorkEvent(&Events.csWorkEvent, true);
		if (Events.m_pWorkEvent != NULL)
			delete Events.m_pWorkEvent;
		Events.m_pWorkEvent = NULL;
	}
}

bool CSimpleWorkerThread::InitInstance()
{
	return true;
}

void CSimpleWorkerThread::ExitInstance()
{
}

void CSimpleWorkerThread::SetCycleTime(UINT nMilliSecs)
{
	dwWaitTime = nMilliSecs;
}

UINT CSimpleWorkerThread::GetCycleTime() const
{
	return dwWaitTime;
}

bool CSimpleWorkerThread::GetExitFlag(void) const
{
	return bTerminate;
}

DWORD CSimpleWorkerThread::GetCurrentThreadID(void) const
{
	CSingleLock lock(const_cast<CCriticalSection *> (&Events.csWorkEvent), true);
	if (pThread != NULL)
		return pThread->m_nThreadID;
	return 0;
}

void CSimpleWorkerThread::SetAlertable(bool bAlertableParam)
{
	bAlertable = bAlertableParam;
}

bool CSimpleWorkerThread::IfActive() const
{
	// if bRunning is false, the thread is either dead, or it is so close to dead that we don't care
	// in other words, the thread proc has returned
	// there are cases, such as in the DLL unload call, where an OS critical section is held which won't
	// allow the thread to fully terminate. While in this state a deadlock occurs if we try to wait
	// for the thread to terminate, so we'll just wait until it returns from the thread proc
	if (!bRunning)
		return false;
	// if bRunning is set, that may not mean that the thread is actually running, since it is possible
	// for the thread to be killed without returning from the thread proc, so test if the thread is running
	DWORD dwExitCode = 0;
	CSingleLock lock(const_cast<CCriticalSection *> (&Events.csWorkEvent), true);
	if (hThread == NULL)
		return false;
	if (!GetExitCodeThread(hThread, &dwExitCode))
		return false;
	if (dwExitCode == STILL_ACTIVE)
		return true;
	if (!CloseHandle(hThread))				// allow the thread to terminate
	{
#ifdef unused
		if (LgtoavlibData.dwMSG_FILE_CLOSE_WARNING != 0)
		{
			DWORD dwError = GetLastError();
			OctopusMsgLog(LOG_POS, LgtoavlibData.dwMSG_FILE_CLOSE_WARNING, dwError, TEXT(""), (LPCTSTR)GetNTErrorText(dwError));
		}
#endif
	}
	*const_cast<HANDLE *>(&hThread) = NULL;
	return false;
}

bool CSimpleWorkerThread::WaitFailed(DWORD dwError)	// over-ride to catch any error after Multi-event wait
{
	(void)dwError;
	return true;					// log error
}

CString CSimpleWorkerThread::GetThreadType(void) const
{
	CString sClassType(typeid(*this).name());
	return sClassType;
}

void CSimpleWorkerThread::GetTerminateTime(FILETIME *pftEndThreadTime) const
{
	ASSERT(pftEndThreadTime != NULL);
	*pftEndThreadTime = ftEndThreadTime;
}

CSimpleWorkerThread *CSimpleWorkerThread::CurrentThread(void)
{
	DWORD dwCurThreadID = GetCurrentThreadId();
	CSimpleWorkerThread *pCurThread;
	ASSERT(pcsGlobalThreadSet != NULL);
	CSingleLock lockGlobalList(pcsGlobalThreadSet, true);
	set<CSimpleWorkerThread *>::iterator itSet;
	for (itSet = pGlobalThreadSetActive->begin(); itSet != pGlobalThreadSetActive->end(); ++itSet)
	{
		pCurThread = *itSet;
		if (pCurThread->dwThreadID == dwCurThreadID)
			return pCurThread;
	}
	return NULL;
}

CCriticalSection *CSimpleWorkerThread::GetGlobalListCriticalSection(void)
{
	return pcsGlobalThreadSet;
}
