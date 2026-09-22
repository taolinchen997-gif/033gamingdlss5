/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "runtime.hpp"
#include <Windows.h>
#include <WinInet.h>

struct scoped_internet_handle
{
	scoped_internet_handle(HINTERNET handle) : handle(handle) {}
	~scoped_internet_handle() { InternetCloseHandle(handle); }

	operator HINTERNET() const { return handle; }

private:
	HINTERNET handle;
};

unsigned int reshade::runtime::s_latest_version[3] = {};

void reshade::runtime::check_for_update()
{
    // 033 distributes a reviewed fork; upstream updates are not this product's
    // release channel. Preserve upstream license/provenance in the package.
}
