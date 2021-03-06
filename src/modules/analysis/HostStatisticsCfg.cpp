/*
 * VERMONT
 * Copyright (C) 2009 Matthias Segschneider <matthias.segschneider@gmx.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "HostStatisticsCfg.h"
#include "core/Cfg.h" // for IllegalEntry

HostStatisticsCfg::HostStatisticsCfg(XMLElement* elem) : CfgHelper<HostStatistics, HostStatisticsCfg>(elem, "hostStatistics")
{
	if (!elem) return;  // needed because of table inside ConfigManager

	try {
		ipSubnet = get("subnet");
		addrFilter = get("addrFilter");
		logPath = get("logPath");
	} catch(IllegalEntry ie) {
		THROWEXCEPTION("Illegal hostStatistics entry in config file");
	}
}

bool HostStatisticsCfg::deriveFrom(HostStatisticsCfg* old)
{
	return false;
}

HostStatisticsCfg* HostStatisticsCfg::create(XMLElement* e)
{
	assert(e);
	assert(e->getName() == getName());
	return new HostStatisticsCfg(e);
}

HostStatistics* HostStatisticsCfg::createInstance()
{
	if (!instance) {
		instance = new HostStatistics(ipSubnet, addrFilter, logPath);
	}
	return instance;
}
