// PluginHelpers.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"
#include "Win32.h"

////////////////////////////////////////////////////////////////////////////////////////////////

using namespace Abstractspoon::Tdl::PluginHelpers;

////////////////////////////////////////////////////////////////////////////////////////////////

HWND Win32::GetHwnd(IntPtr hWnd)
{
	return static_cast<HWND>(hWnd.ToPointer());
}

void Win32::RemoveClientEdge(IntPtr hWnd)
{
	// remove client edge
	int nExStyle = GetWindowLong(GetHwnd(hWnd), GWL_EXSTYLE);

	if ((nExStyle & WS_EX_CLIENTEDGE) == WS_EX_CLIENTEDGE)
	{
		SetWindowLong(GetHwnd(hWnd), GWL_EXSTYLE, (nExStyle & ~WS_EX_CLIENTEDGE));
		SetWindowPos(GetHwnd(hWnd), NULL, 0, 0, 0, 0,
			SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
}

void Win32::AddClientEdge(IntPtr hWnd)
{
	// remove client edge
	int nExStyle = GetWindowLong(GetHwnd(hWnd), GWL_EXSTYLE);

	if ((nExStyle & WS_EX_CLIENTEDGE) == 0)
	{
		SetWindowLong(GetHwnd(hWnd), GWL_EXSTYLE, (nExStyle | WS_EX_CLIENTEDGE));
		SetWindowPos(GetHwnd(hWnd), NULL, 0, 0, 0, 0,
			SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
}

void Win32::RemoveBorder(IntPtr hWnd)
{
	// remove client edge
	int nStyle = GetWindowLong(GetHwnd(hWnd), GWL_STYLE);

	if ((nStyle & WS_BORDER) == WS_BORDER)
	{
		SetWindowLong(GetHwnd(hWnd), GWL_STYLE, (nStyle & ~WS_BORDER));
		SetWindowPos(GetHwnd(hWnd), NULL, 0, 0, 0, 0,
			SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
}

void Win32::AddBorder(IntPtr hWnd)
{
	// remove client edge
	int nStyle = GetWindowLong(GetHwnd(hWnd), GWL_STYLE);

	if ((nStyle & WS_BORDER) == 0)
	{
		SetWindowLong(GetHwnd(hWnd), GWL_STYLE, (nStyle | WS_BORDER));
		SetWindowPos(GetHwnd(hWnd), NULL, 0, 0, 0, 0,
			SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
			SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
}

int Win32::GetVScrollPos(IntPtr hWnd)
{
	return ::GetScrollPos(GetHwnd(hWnd), SB_VERT);
}

int Win32::GetHScrollPos(IntPtr hWnd)
{
	return ::GetScrollPos(GetHwnd(hWnd), SB_HORZ);
}

int Win32::GetSystemDPI()
{
	static int nDPI = 0;

	if (nDPI == 0)
	{
		HDC	hdc = ::GetDC(NULL);

		nDPI = GetDeviceCaps(hdc, LOGPIXELSX);

		::ReleaseDC(NULL, hdc);
	}

	return nDPI;
}

////////////////////////////////////////////////////////////////////////////////////////////////

DlgUnits::DlgUnits(IntPtr hWnd) 
	: 
	m_hWnd(Win32::GetHwnd(hWnd)),
	m_DlgBaseUnitsX(0),
	m_DlgBaseUnitsY(0)
{
}

DlgUnits::DlgUnits() : m_hWnd(NULL)
{
	m_hWnd = NULL;
	DWORD dwDLBU = ::GetDialogBaseUnits();

	m_DlgBaseUnitsX = LOWORD(dwDLBU);
	m_DlgBaseUnitsY = HIWORD(dwDLBU);
}

int DlgUnits::ToPixelsX(int x)
{
	int unused;
	ToPixels(x, unused);

	return x;
}

int DlgUnits::ToPixelsY(int y)
{
	int unused;
	ToPixels(unused, y);

	return y;
}

void DlgUnits::ToPixels(int& x, int& y)
{
	if (m_hWnd)
	{
		RECT rect = { 0, 0, x, y };
		::MapDialogRect(m_hWnd, &rect);

		x = rect.right;
		y = rect.bottom;
	}
	else
	{
		x = MulDiv(x, m_DlgBaseUnitsX, 4);
		y = MulDiv(y, m_DlgBaseUnitsY, 8);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////
