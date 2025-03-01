/*
 * Copyright: JessMA Open Source (ldcsaa@gmail.com)
 *
 * Author	: Bruce Liang
 * Website	: https://github.com/ldcsaa
 * Project	: https://github.com/ldcsaa/HP-Socket
 * Blog		: http://www.cnblogs.com/ldcsaa
 * Wiki		: http://www.oschina.net/p/hp-socket
 * QQ Group	: 44636872, 75375912
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 
#include "HttpClient.h"

#ifdef _HTTP_SUPPORT

template<class R, class T, USHORT default_port> BOOL CHttpClientT<R, T, default_port>::CheckParams()
{
	if(m_enLocalVersion != HV_1_1 && m_enLocalVersion != HV_1_0)
	{
		SetLastError(SE_INVALID_PARAM, __FUNCTION__, ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	return __super::CheckParams();
}

template<class R, class T, USHORT default_port> BOOL CHttpClientT<R, T, default_port>::SendRequest(LPCSTR lpszMethod, LPCSTR lpszPath, const THeader lpHeaders[], int iHeaderCount, const BYTE* pBody, int iLength)
{
	USES_CONVERSION;

	WSABUF szBuffer[2];
	CStringA strHeader;

	LPCSTR lpszHost	= nullptr;
	USHORT usPort	= 0;
	BOOL bConnect	= (stricmp(lpszMethod, HTTP_METHOD_CONNECT) == 0);

	if(!bConnect)
	{
		GetRemoteHost(&lpszHost, &usPort);
		if(usPort == default_port) usPort = 0;
	}

	CStringA strPath;
	::AdjustRequestPath(bConnect, lpszPath, strPath);

	m_objHttp.SetRequestPath(lpszMethod, strPath);
	m_objHttp.ReloadCookies();

	::MakeRequestLine(lpszMethod, strPath, m_enLocalVersion, strHeader);
	::MakeHeaderLines(lpHeaders, iHeaderCount, &m_objHttp.GetCookieMap(), iLength, TRUE, -1, lpszHost, usPort, strHeader);
	::MakeHttpPacket(strHeader, pBody, iLength, szBuffer);

	return SendPackets(szBuffer, 2);
}

template<class R, class T, USHORT default_port> BOOL CHttpClientT<R, T, default_port>::SendLocalFile(LPCSTR lpszFileName, LPCSTR lpszMethod, LPCSTR lpszPath, const THeader lpHeaders[], int iHeaderCount)
{
	CFile file;
	CFileMapping fmap;

	HRESULT hr = ::ReadSmallFile(CA2T(lpszFileName), file, fmap);

	if(FAILED(hr))
	{
		::SetLastError(hr);
		return FALSE;
	}

	return SendRequest(lpszMethod, lpszPath, lpHeaders, iHeaderCount, (BYTE*)fmap, (int)fmap.Size());
}

template<class R, class T, USHORT default_port> BOOL CHttpClientT<R, T, default_port>::SendChunkData(const BYTE* pData, int iLength, LPCSTR lpszExtensions)
{
	char szLen[12];
	WSABUF bufs[5];

	int iCount = MakeChunkPackage(pData, iLength, lpszExtensions, szLen, bufs);

	return SendPackets(bufs, iCount);
}

template<class R, class T, USHORT default_port> BOOL CHttpClientT<R, T, default_port>::SendWSMessage(BOOL bFinal, BYTE iReserved, BYTE iOperationCode, const BYTE lpszMask[4], const BYTE* pData, int iLength, ULONGLONG ullBodyLen)
{
	ASSERT(lpszMask);

	WSABUF szBuffer[2];
	BYTE szHeader[HTTP_MAX_WS_HEADER_LEN];

	unique_ptr<BYTE[]> szData = make_unique<BYTE[]>(iLength);
	memcpy(szData.get(), pData, iLength);

	if(!::MakeWSPacket(bFinal, iReserved, iOperationCode, lpszMask, szData.get(), iLength, ullBodyLen, szHeader, szBuffer))
		return FALSE;

	return SendPackets(szBuffer, 2);
}

template<class R, class T, USHORT default_port> BOOL CHttpClientT<R, T, default_port>::StartHttp()
{
	if(IsHttpAutoStart())
	{
		::SetLastError(ERROR_INVALID_OPERATION);
		return FALSE;
	}

	if(!IsConnected())
	{
		::SetLastError(ERROR_INVALID_STATE);
		return FALSE;
	}

	CReentrantCriSecLock locallock(m_csHttp);

	if(!IsConnected())
	{
		::SetLastError(ERROR_INVALID_STATE);
		return FALSE;
	}

	if(m_objHttp.IsValid())
	{
		::SetLastError(ERROR_ALREADY_INITIALIZED);
		return FALSE;
	}

	DoStartHttp();

	if(!IsSecure())
		FireHandShake();
	else
	{
#ifdef _SSL_SUPPORT
		if(IsSSLAutoHandShake())
			StartSSLHandShakeNoCheck();
#endif
	}

	return TRUE;
}

// ------------------------------------------------------------------------------------------------------------- //

template<class T, USHORT default_port> BOOL CHttpSyncClientT<T, default_port>::Start(LPCTSTR lpszRemoteAddress, USHORT usPort, BOOL bAsyncConnect, LPCTSTR lpszBindAddress, USHORT usLocalPort)
{
	CleanupRequestResult();

	if(!__super::Start(lpszRemoteAddress, usPort, TRUE, lpszBindAddress, usLocalPort))
		return FALSE;

	BOOL isOK = WaitForEvent(m_dwConnectTimeout);

	if(!isOK || m_enProgress != HSRP_DONE)
	{
		int ec = m_enProgress == HSRP_WAITING ? ERROR_TIMEDOUT : ERROR_CONNREFUSED;

		if(!isOK) Stop();

		SetLastError(SE_CONNECT_SERVER, __FUNCTION__, ec);
		return FALSE;
	}

	return TRUE;
}

template<class T, USHORT default_port> BOOL CHttpSyncClientT<T, default_port>::SendRequest(LPCSTR lpszMethod, LPCSTR lpszPath, const THeader lpHeaders[], int iHeaderCount, const BYTE* pBody, int iLength)
{
	CleanupRequestResult();

	if(!__super::SendRequest(lpszMethod, lpszPath, lpHeaders, iHeaderCount, pBody, iLength))
		return FALSE;

	BOOL isOK = WaitForEvent(m_dwRequestTimeout);

	if(!isOK || m_enProgress != HSRP_DONE)
	{
		int ec = m_enProgress == HSRP_WAITING ? ERROR_TIMEDOUT :
				(m_enProgress == HSRP_CLOSE ? ERROR_CONNABORTED : ERROR_INVALID_DATA);

		if(!isOK) Stop();

		SetLastError(SE_DATA_SEND, __FUNCTION__, ec);
		return FALSE;
	}

	return TRUE;
}

template<class T, USHORT default_port> BOOL CHttpSyncClientT<T, default_port>::SendWSMessage(BOOL bFinal, BYTE iReserved, BYTE iOperationCode, const BYTE lpszMask[4], const BYTE* pData, int iLength, ULONGLONG ullBodyLen)
{
	CleanupRequestResult();

	if(!__super::SendWSMessage(bFinal, iReserved, iOperationCode, lpszMask, pData, iLength, ullBodyLen))
		return FALSE;

	BOOL isOK = WaitForEvent(m_dwRequestTimeout);

	if(!isOK || m_enProgress != HSRP_DONE)
	{
		int ec = m_enProgress == HSRP_WAITING ? ERROR_TIMEDOUT :
				(m_enProgress == HSRP_CLOSE ? ERROR_CONNABORTED : ERROR_INVALID_DATA);

		if(!isOK) Stop();

		SetLastError(SE_DATA_SEND, __FUNCTION__, ec);
		return FALSE;
	}

	return TRUE;
}

template<class T, USHORT default_port> BOOL CHttpSyncClientT<T, default_port>::OpenUrl(LPCSTR lpszMethod, LPCSTR lpszUrl, const THeader lpHeaders[], int iHeaderCount, const BYTE* pBody, int iLength, BOOL bForceReconnect)
{
	BOOL		bHttps;
	USHORT		usPort;
	CStringA	strHost;
	CStringA	strPath;

	if(!IsHttpAutoStart())
	{
		SetLastError(SE_INVALID_PARAM, __FUNCTION__, ERROR_NOT_SUPPORTED);
		return FALSE;
	}

	if(!::ParseUrl(lpszUrl, bHttps, strHost, usPort, strPath))
	{
		SetLastError(SE_CONNECT_SERVER, __FUNCTION__, ERROR_ADDRNOTAVAIL);
		return FALSE;
	}

	if((bHttps && default_port == HTTP_DEFAULT_PORT) || (!bHttps && default_port == HTTPS_DEFAULT_PORT))
	{
		SetLastError(SE_CONNECT_SERVER, __FUNCTION__, ERROR_PROTO);
		return FALSE;
	}

	if(HasStarted())
	{
		BOOL bNeedStop = bForceReconnect;

		if(!bNeedStop)
		{
			LPCSTR lpszHost	= nullptr;
			USHORT usPort2	= 0;

			GetRemoteHost(&lpszHost, &usPort2);

			if(usPort != usPort2)
				bNeedStop = TRUE;
			else
			{
				HP_SCOPE_HOST host(CA2T((LPCSTR)strHost));

				if(lstricmp(host.name, CA2T(lpszHost)) != 0)
					bNeedStop = TRUE;
			}
		}

		if(bNeedStop) Stop();
	}

	EnServiceState state = GetState();

	if(state != SS_STARTED)
	{
		if(state == SS_STARTING)
		{
			do 
			{
				::WaitFor(10);
				state = GetState();
			} while(state != SS_STARTED && state != SS_STOPPED);
		}
		else
		{
			while(state != SS_STOPPED)
			{
				::WaitFor(10);
				state = GetState();
			}

			Start(CA2T(strHost), usPort, FALSE, nullptr);
			state = GetState();
		}

		if(state == SS_STOPPED)
			return FALSE;
	}

	if(iLength < 0 && !::IsStrEmptyA((LPCSTR)pBody))
		return SendLocalFile((LPCSTR)pBody, lpszMethod, strPath, lpHeaders, iHeaderCount);

	return SendRequest(lpszMethod, strPath, lpHeaders, iHeaderCount, pBody, iLength);
}

template<class T, USHORT default_port> BOOL CHttpSyncClientT<T, default_port>::CleanupRequestResult()
{
	m_pHttpObj	 = &m_objHttp;
	m_enProgress = HSRP_WAITING;

	m_szBuffer.Free();
	m_objHttp2.Reset();
	m_evWait.Reset();
	
	return TRUE;
}

template<class T, USHORT default_port> void CHttpSyncClientT<T, default_port>::SetRequestEvent(EnHttpSyncRequestProgress enProgress, BOOL bCopyHttpObj)
{
	if(m_enProgress != HSRP_WAITING)
		return;

	m_enProgress = enProgress;

	if(bCopyHttpObj)
	{
		m_objHttp2.CopyData(m_objHttp);
		m_objHttp2.CopyWSContext(m_objHttp);

		m_pHttpObj = &m_objHttp2;
	}

	m_evWait.Set();
}

template<class T, USHORT default_port> BOOL CHttpSyncClientT<T, default_port>::WaitForEvent(DWORD dwWait)
{
	LONG lTimeout = INFINITE;

	if(dwWait != 0)
		lTimeout  = (LONG)dwWait;

	return (m_evWait.Wait(lTimeout) > TIMEOUT);
}

template<class T, USHORT default_port> BOOL CHttpSyncClientT<T, default_port>::GetResponseBody(LPCBYTE* lpszBody, int* iLength)
{
	ASSERT(lpszBody && iLength);

	*lpszBody = m_szBuffer.Ptr();
	*iLength  = (int)m_szBuffer.Size();

	return TRUE;
}

template<class T, USHORT default_port> EnHandleResult CHttpSyncClientT<T, default_port>::OnHandShake(ITcpClient* pSender, CONNID dwConnID)
{
	EnHandleResult rs = HR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnHandShake(pSender, dwConnID);

	if(rs != HR_ERROR)
		SetRequestEvent(HSRP_DONE, FALSE);

	return rs;
}

template<class T, USHORT default_port> EnHandleResult CHttpSyncClientT<T, default_port>::OnClose(ITcpClient* pSender, CONNID dwConnID, EnSocketOperation enOperation, int iErrorCode)
{
	EnHandleResult rs = HR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnClose(pSender, dwConnID, enOperation, iErrorCode);

	SetRequestEvent(HSRP_CLOSE);

	return rs;
}

template<class T, USHORT default_port> EnHttpParseResult CHttpSyncClientT<T, default_port>::OnBody(IHttpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
{
	EnHttpParseResult rs = HPR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnBody(pSender, dwConnID, pData, iLength);

	if(rs != HPR_ERROR)
		m_szBuffer.Cat(pData, iLength);

	return rs;
}

template<class T, USHORT default_port> EnHttpParseResult CHttpSyncClientT<T, default_port>::OnMessageComplete(IHttpClient* pSender, CONNID dwConnID)
{
	EnHttpParseResult rs = HPR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnMessageComplete(pSender, dwConnID);

	if(rs != HPR_ERROR)
	{
		if(GetUpgradeType() == HUT_NONE)
			SetRequestEvent(HSRP_DONE);
	}

	return rs;
}

template<class T, USHORT default_port> EnHttpParseResult CHttpSyncClientT<T, default_port>::OnUpgrade(IHttpClient* pSender, CONNID dwConnID, EnHttpUpgradeType enUpgradeType)
{
	EnHttpParseResult rs = HPR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnUpgrade(pSender, dwConnID, enUpgradeType);

	if(rs != HPR_ERROR)
	{
		if(enUpgradeType == HUT_WEB_SOCKET)
		{
			SetRequestEvent(HSRP_DONE);
			rs = HPR_OK;
		}
		else
		{
			SetRequestEvent(HSRP_ERROR);
			rs = HPR_ERROR;
		}
	}

	return rs;
}

template<class T, USHORT default_port> EnHttpParseResult CHttpSyncClientT<T, default_port>::OnParseError(IHttpClient* pSender, CONNID dwConnID, int iErrorCode, LPCSTR lpszErrorDesc)
{
	EnHttpParseResult rs = HPR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnParseError(pSender, dwConnID, iErrorCode, lpszErrorDesc);

	SetRequestEvent(HSRP_ERROR);

	return rs;
}

template<class T, USHORT default_port> EnHandleResult CHttpSyncClientT<T, default_port>::OnWSMessageBody(IHttpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
{
	EnHandleResult rs = HR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnWSMessageBody(pSender, dwConnID, pData, iLength);

	if(rs != HR_ERROR)
		m_szBuffer.Cat(pData, iLength);

	return rs;
}

template<class T, USHORT default_port> EnHandleResult CHttpSyncClientT<T, default_port>::OnWSMessageComplete(IHttpClient* pSender, CONNID dwConnID)
{
	EnHandleResult rs = HR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnWSMessageComplete(pSender, dwConnID);

	if(rs != HR_ERROR)
		SetRequestEvent(HSRP_DONE);

	return rs;
}

template<class T, USHORT default_port> EnHandleResult CHttpSyncClientT<T, default_port>::OnPrepareConnect(ITcpClient* pSender, CONNID dwConnID, SOCKET socket)
{
	EnHandleResult rs = HR_OK;

	if(m_pListener2 != nullptr)
		return m_pListener2->OnPrepareConnect(pSender, dwConnID, socket);

	return rs;
}

template<class T, USHORT default_port> EnHandleResult CHttpSyncClientT<T, default_port>::OnConnect(ITcpClient* pSender, CONNID dwConnID)
{
	EnHandleResult rs = HR_OK;

	if(m_pListener2 != nullptr)
		return m_pListener2->OnConnect(pSender, dwConnID);

	return rs;
}

template<class T, USHORT default_port> EnHandleResult CHttpSyncClientT<T, default_port>::OnSend(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
{
	EnHandleResult rs = HR_OK;

	if(m_pListener2 != nullptr)
		return m_pListener2->OnSend(pSender, dwConnID, pData, iLength);

	return rs;
}

template<class T, USHORT default_port> EnHandleResult CHttpSyncClientT<T, default_port>::OnReceive(ITcpClient* pSender, CONNID dwConnID, const BYTE* pData, int iLength)
{
	EnHandleResult rs = HR_OK;

	if(m_pListener2 != nullptr)
		return m_pListener2->OnReceive(pSender, dwConnID, pData, iLength);

	return rs;
}

template<class T, USHORT default_port> EnHttpParseResult CHttpSyncClientT<T, default_port>::OnMessageBegin(IHttpClient* pSender, CONNID dwConnID)
{
	EnHttpParseResult rs = HPR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnMessageBegin(pSender, dwConnID);

	return rs;
}

template<class T, USHORT default_port> EnHttpParseResult CHttpSyncClientT<T, default_port>::OnStatusLine(IHttpClient* pSender, CONNID dwConnID, USHORT usStatusCode, LPCSTR lpszDesc)
{
	EnHttpParseResult rs = HPR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnStatusLine(pSender, dwConnID, usStatusCode, lpszDesc);

	return rs;
}

template<class T, USHORT default_port> EnHttpParseResult CHttpSyncClientT<T, default_port>::OnHeader(IHttpClient* pSender, CONNID dwConnID, LPCSTR lpszName, LPCSTR lpszValue)
{
	EnHttpParseResult rs = HPR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnHeader(pSender, dwConnID, lpszName, lpszValue);

	return rs;
}

template<class T, USHORT default_port> EnHttpParseResult CHttpSyncClientT<T, default_port>::OnHeadersComplete(IHttpClient* pSender, CONNID dwConnID)
{
	EnHttpParseResult rs = HPR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnHeadersComplete(pSender, dwConnID);

	return rs;
}

template<class T, USHORT default_port> EnHttpParseResult CHttpSyncClientT<T, default_port>::OnChunkHeader(IHttpClient* pSender, CONNID dwConnID, int iLength)
{
	EnHttpParseResult rs = HPR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnChunkHeader(pSender, dwConnID, iLength);

	return rs;
}

template<class T, USHORT default_port> EnHttpParseResult CHttpSyncClientT<T, default_port>::OnChunkComplete(IHttpClient* pSender, CONNID dwConnID)
{
	EnHttpParseResult rs = HPR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnChunkComplete(pSender, dwConnID);

	return rs;
}

template<class T, USHORT default_port> EnHandleResult CHttpSyncClientT<T, default_port>::OnWSMessageHeader(IHttpClient* pSender, CONNID dwConnID, BOOL bFinal, BYTE iReserved, BYTE iOperationCode, const BYTE lpszMask[4], ULONGLONG ullBodyLen)
{
	EnHandleResult rs = HR_OK;

	if(m_pListener2 != nullptr)
		rs = m_pListener2->OnWSMessageHeader(pSender, dwConnID, bFinal, iReserved, iOperationCode, lpszMask, ullBodyLen);

	return rs;
}

// ------------------------------------------------------------------------------------------------------------- //

template class CHttpClientT<IHttpRequester, CTcpClient, HTTP_DEFAULT_PORT>;
template class CHttpSyncClientT<CTcpClient, HTTP_DEFAULT_PORT>;

#ifdef _SSL_SUPPORT

#include "SSLClient.h"

template class CHttpClientT<IHttpRequester, CSSLClient, HTTPS_DEFAULT_PORT>;
template class CHttpSyncClientT<CSSLClient, HTTPS_DEFAULT_PORT>;

#endif

#endif