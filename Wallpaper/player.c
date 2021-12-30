#include"player.h"

static IGraphBuilder* m_pGraph;
static IMediaControl* m_pControl;
static IMediaEventEx* m_pEvent;
static IVMRWindowlessControl9* m_pWindowless;

static void TearDownGraph();
static HRESULT InitializeGraph(HWND hwnd);
static HRESULT RenderStreams(IBaseFilter* pSource, HWND hwnd);
static HRESULT AddToGraph(IGraphBuilder* pGraph, HWND hwnd);
static HRESULT AddFilterByCLSID(IGraphBuilder* pGraph, REFGUID clsid, IBaseFilter** ppF, LPCWSTR wszName);
static HRESULT InitWindowlessVMR9(IBaseFilter* pVMR, HWND hwnd, IVMRWindowlessControl9** ppWC);
static HRESULT FinalizeGraph(IGraphBuilder* pGraph);
static HRESULT RemoveUnconnectedRenderer(IGraphBuilder* pGraph, IBaseFilter* pRenderer, BOOL* pbRemoved);
static HRESULT FindConnectedPin(IBaseFilter* pFilter, PIN_DIRECTION PinDir, IPin** ppPin);
static HRESULT IsPinConnected(IPin* pPin, BOOL* pResult);
static HRESULT IsPinDirection(IPin* pPin, PIN_DIRECTION dir, BOOL* pResult);

int init_player()
{
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);

	return FAILED(hr) ? -1 : 0;
}

void uninit_player()
{
	CoUninitialize();
}

int open_video(HWND hwnd, LPCWSTR file_name)
{
	IBaseFilter* pSource = NULL;

	HRESULT hr = InitializeGraph(hwnd);
	if (FAILED(hr))
		goto done;

	hr = m_pGraph->lpVtbl->AddSourceFilter(m_pGraph, file_name, NULL, &pSource);
	if (FAILED(hr))
		goto done;

	hr = RenderStreams(pSource, hwnd);

done:
	if (FAILED(hr))
		TearDownGraph();
	if (pSource)
	{
		pSource->lpVtbl->Release(pSource);
		pSource = NULL;
	}
	return hr;
}

HRESULT update_video_window(HWND hwnd, const LPRECT prc)
{
	if (m_pWindowless == NULL)
		return S_OK;

	if (prc)
		return m_pWindowless->lpVtbl->SetVideoPosition(m_pWindowless, NULL, prc);

	RECT rc;
	GetClientRect(hwnd, &rc);
	return m_pWindowless->lpVtbl->SetVideoPosition(m_pWindowless, NULL, &rc);
}

HRESULT repaint(HWND hwnd, HDC hdc)
{
	if (m_pWindowless)
		return m_pWindowless->lpVtbl->RepaintVideo(m_pWindowless, hwnd, hdc);

	return S_OK;
}

HRESULT play()
{
	return  m_pControl->lpVtbl->Run(m_pControl);
}

HRESULT pause()
{
	return  m_pControl->lpVtbl->Pause(m_pControl);
}

HRESULT stop()
{
	return  m_pControl->lpVtbl->Stop(m_pControl);
}

HRESULT HandleGraphEvent(GraphEventFN pfnOnGraphEvent, HWND hwnd)
{
	long evCode = 0;
	LONG_PTR param1 = 0, param2 = 0;
	HRESULT hr = S_OK;

	if (!m_pEvent)
		return E_UNEXPECTED;

	while (SUCCEEDED(m_pEvent->lpVtbl->GetEvent(m_pEvent, &evCode, &param1, &param2, 0)))
	{
		pfnOnGraphEvent(hwnd, evCode, param1, param2);

		hr = m_pEvent->lpVtbl->FreeEventParams(m_pEvent, evCode, param1, param2);
		if (FAILED(hr))
			break;
	}

	return hr;
}

void CALLBACK OnGraphEvent(HWND hwnd, long evCode, LONG_PTR param1, LONG_PTR param2)
{
	switch (evCode)
	{
	case EC_COMPLETE:
	case EC_USERABORT:
		stop();
		break;
	case EC_ERRORABORT:
		stop();
		break;
	}
}

HRESULT DisplayModeChanged()
{
	return m_pWindowless ? m_pWindowless->lpVtbl->DisplayModeChanged(m_pWindowless) : S_OK;
}

static HRESULT RemoveUnconnectedRenderer(IGraphBuilder* pGraph, IBaseFilter* pRenderer, BOOL* pbRemoved)
{
	IPin* pPin = NULL;
	*pbRemoved = FALSE;

	HRESULT hr = FindConnectedPin(pRenderer, PINDIR_INPUT, &pPin);
	if (pPin)
	{
		pPin->lpVtbl->Release(pPin);
		pPin = NULL;
	}

	if (FAILED(hr))
	{
		hr = pGraph->lpVtbl->RemoveFilter(pGraph, pRenderer);
		*pbRemoved = TRUE;
	}

	return hr;
}

static HRESULT FindConnectedPin(IBaseFilter* pFilter, PIN_DIRECTION PinDir, IPin** ppPin)
{
	*ppPin = NULL;
	IEnumPins* pEnum = NULL;
	IPin* pPin = NULL;

	HRESULT hr = pFilter->lpVtbl->EnumPins(pFilter, &pEnum);
	if (FAILED(hr))
		return hr;

	BOOL bFound = FALSE;
	while (S_OK == pEnum->lpVtbl->Next(pEnum, 1, &pPin, NULL))
	{
		BOOL bIsConnected;

		hr = IsPinConnected(pPin, &bIsConnected);
		if (SUCCEEDED(hr) && bIsConnected)
			hr = IsPinDirection(pPin, PinDir, &bFound);

		if (FAILED(hr))
		{
			pPin->lpVtbl->Release(pPin);
			break;
		}
		if (bFound)
		{
			*ppPin = pPin;
			break;
		}

		pPin->lpVtbl->Release(pPin);
	}

	pEnum->lpVtbl->Release(pEnum);

	if (!bFound)
		hr = VFW_E_NOT_FOUND;

	return hr;
}

static HRESULT IsPinConnected(IPin* pPin, BOOL* pResult)
{
	IPin* pTmp = NULL;
	HRESULT hr = pPin->lpVtbl->ConnectedTo(pPin, &pTmp);
	if (SUCCEEDED(hr))
		*pResult = TRUE;
	else if (hr == VFW_E_NOT_CONNECTED)
	{
		*pResult = FALSE;
		hr = S_OK;
	}

	if (pTmp)
	{
		pTmp->lpVtbl->Release(pTmp);
		pTmp = NULL;
	}

	return hr;
}

static HRESULT IsPinDirection(IPin* pPin, PIN_DIRECTION dir, BOOL* pResult)
{
	PIN_DIRECTION pinDir;
	HRESULT hr = pPin->lpVtbl->QueryDirection(pPin, &pinDir);
	if (SUCCEEDED(hr))
		*pResult = (pinDir == dir);

	return hr;
}

static HRESULT InitWindowlessVMR9(IBaseFilter* pVMR, HWND hwnd, IVMRWindowlessControl9** ppWC)
{
	IVMRFilterConfig9* pConfig = NULL;
	IVMRWindowlessControl9* pWC = NULL;

	HRESULT hr = pVMR->lpVtbl->QueryInterface(pVMR, &IID_IVMRFilterConfig9, (void**)(&pConfig));
	if (FAILED(hr))
		goto done;

	hr = pConfig->lpVtbl->SetRenderingMode(pConfig, VMR9Mode_Windowless);
	if (FAILED(hr))
		goto done;

	hr = pVMR->lpVtbl->QueryInterface(pVMR, &IID_IVMRWindowlessControl9, (void**)(&pWC));
	if (FAILED(hr))
		goto done;

	hr = pWC->lpVtbl->SetVideoClippingWindow(pWC, hwnd);
	if (FAILED(hr))
		goto done;

	hr = pWC->lpVtbl->SetAspectRatioMode(pWC, VMR9ARMode_LetterBox);
	if (FAILED(hr))
		goto done;

	*ppWC = pWC;
	(*ppWC)->lpVtbl->AddRef(*ppWC);

done:
	if (pConfig)
	{
		pConfig->lpVtbl->Release(pConfig);
		pConfig = NULL;
	}
	if (pWC)
	{
		pWC->lpVtbl->Release(pWC);
		pWC = NULL;
	}

	return hr;
}

static HRESULT FinalizeGraph(IGraphBuilder* pGraph)
{
	IBaseFilter* pFilter = NULL;
	BOOL bRemoved;

	if (m_pWindowless == NULL)
		return S_OK;

	HRESULT hr = m_pWindowless->lpVtbl->QueryInterface(m_pWindowless, &IID_IBaseFilter, (void**)(&pFilter));
	if (FAILED(hr))
		goto done;

	hr = RemoveUnconnectedRenderer(pGraph, pFilter, &bRemoved);

	if (bRemoved && m_pWindowless)
	{
		m_pWindowless->lpVtbl->Release(m_pWindowless);
		m_pWindowless = NULL;
	}

done:
	if (pFilter)
	{
		pFilter->lpVtbl->Release(pFilter);
		pFilter = NULL;
	}

	return hr;
}

static HRESULT AddFilterByCLSID(IGraphBuilder* pGraph, REFGUID clsid, IBaseFilter** ppF, LPCWSTR wszName)
{
	*ppF = 0;
	IBaseFilter* pFilter = NULL;

	HRESULT hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER,
		&IID_IBaseFilter, (void**)(&pFilter));
	if (FAILED(hr))
		goto done;

	hr = pGraph->lpVtbl->AddFilter(pGraph, pFilter, wszName);
	if (FAILED(hr))
		goto done;

	*ppF = pFilter;
	(*ppF)->lpVtbl->AddRef(*ppF);

done:
	if (pFilter)
	{
		pFilter->lpVtbl->Release(pFilter);
		pFilter = NULL;
	}

	return hr;
}

static HRESULT AddToGraph(IGraphBuilder* pGraph, HWND hwnd)
{
	IBaseFilter* pVMR = NULL;

	HRESULT hr = AddFilterByCLSID(pGraph, &CLSID_VideoMixingRenderer9,
		&pVMR, L"VMR-9");
	if (SUCCEEDED(hr))
		hr = InitWindowlessVMR9(pVMR, hwnd, &m_pWindowless);

	if (pVMR)
	{
		pVMR->lpVtbl->Release(pVMR);
		pVMR = NULL;
	}

	return hr;
}

static HRESULT RenderStreams(IBaseFilter* pSource, HWND hwnd)
{
	BOOL bRenderedAnyPin = FALSE;
	IFilterGraph2* pGraph2 = NULL;
	IEnumPins* pEnum = NULL;
	IBaseFilter* pAudioRenderer = NULL;

	HRESULT hr = m_pGraph->lpVtbl->QueryInterface(m_pGraph,
		&IID_IFilterGraph2, (void**)(&pGraph2));
	if (FAILED(hr))
		goto done;

	hr = AddToGraph(m_pGraph, hwnd);
	if (FAILED(hr))
		goto done;

	hr = AddFilterByCLSID(m_pGraph, &CLSID_DSoundRender,
		&pAudioRenderer, L"Audio Renderer");
	if (FAILED(hr))
		goto done;

	hr = pSource->lpVtbl->EnumPins(pSource, &pEnum);
	if (FAILED(hr))
		goto done;

	IPin* pPin;
	while (S_OK == pEnum->lpVtbl->Next(pEnum, 1, &pPin, NULL))
	{
		HRESULT hr2 = pGraph2->lpVtbl->RenderEx(pGraph2, pPin, AM_RENDEREX_RENDERTOEXISTINGRENDERERS, NULL);

		pPin->lpVtbl->Release(pPin);
		if (SUCCEEDED(hr2))
			bRenderedAnyPin = TRUE;
	}

	hr = FinalizeGraph(m_pGraph);
	if (FAILED(hr))
		goto done;

	// Remove the audio renderer, if not used.
	BOOL bRemoved;
	hr = RemoveUnconnectedRenderer(m_pGraph, pAudioRenderer, &bRemoved);

done:
	if (pEnum)
	{
		pEnum->lpVtbl->Release(pEnum);
		pEnum = NULL;
	}
	if (pAudioRenderer)
	{
		pAudioRenderer->lpVtbl->Release(pAudioRenderer);
		pAudioRenderer = NULL;
	}
	if (pGraph2)
	{
		pGraph2->lpVtbl->Release(pGraph2);
		pGraph2 = NULL;
	}

	if (SUCCEEDED(hr) && !bRenderedAnyPin)
		hr = VFW_E_CANNOT_RENDER;

	return hr;
}

static HRESULT InitializeGraph(HWND hwnd)
{
	TearDownGraph();

	HRESULT hr = CoCreateInstance(&CLSID_FilterGraph, NULL,
		CLSCTX_INPROC_SERVER, &IID_IGraphBuilder, (void**)&m_pGraph);
	if (FAILED(hr))
		goto done;

	hr = m_pGraph->lpVtbl->QueryInterface(m_pGraph, &IID_IMediaControl, (void**)&m_pControl);
	if (FAILED(hr))
		goto done;

	hr = m_pGraph->lpVtbl->QueryInterface(m_pGraph, &IID_IMediaEventEx, (void**)(&m_pEvent));
	if (FAILED(hr))
		goto done;

	// Set up event notification.
	hr = m_pEvent->lpVtbl->SetNotifyWindow(m_pEvent, (OAHWND)hwnd, WM_GRAPH_EVENT, NULL);
	if (FAILED(hr))
		goto done;

done:
	return hr;
}

static void TearDownGraph()
{
	if (m_pEvent)
		m_pEvent->lpVtbl->SetNotifyWindow(m_pEvent, (OAHWND)NULL, NULL, NULL);

	if (m_pGraph)
	{
		m_pGraph->lpVtbl->Release(m_pGraph);
		m_pGraph = NULL;
	}
	if (m_pControl)
	{
		m_pControl->lpVtbl->Release(m_pControl);
		m_pControl = NULL;
	}
	if (m_pEvent)
	{
		m_pEvent->lpVtbl->Release(m_pEvent);
		m_pEvent = NULL;
	}
}