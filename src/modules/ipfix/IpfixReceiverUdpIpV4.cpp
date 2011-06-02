/*
 * IPFIX Concentrator Module Library
 * Copyright (C) 2004 Christoph Sommer <http://www.deltadevelopment.de/users/christoph/ipfix/>
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
 *
 */

#include "IpfixReceiverUdpIpV4.hpp"

#include "IpfixPacketProcessor.hpp"
#include "common/ipfixlolib/ipfix.h"
#include "common/msg.h"

#include <stdexcept>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sstream>


using namespace std;

/** 
 * Does UDP/IPv4 specific initialization.
 * @param port Port to listen on
 * @param ipAddr interface to use, if equals "", all interfaces will be used
 */
IpfixReceiverUdpIpV4::IpfixReceiverUdpIpV4(int port, std::string ipAddr)
	: statReceivedPackets(0)
{
	receiverPort = port;

	if (ipAddr == "") {
		createIPv4Socket(ipAddr, SOCK_DGRAM, 0, port);
		createIPv6Socket(ipAddr, SOCK_DGRAM, 0, port);
	} else {
		enum receiver_address_type addrType;
		addrType = getAddressType(ipAddr);
		if (addrType == IPv4_ADDRESS) {
			createIPv4Socket(ipAddr, SOCK_DGRAM, 0, port);
		} else if (addrType == IPv6_ADDRESS) {
			createIPv6Socket(ipAddr, SOCK_DGRAM, 0, port);
		} else {
			THROWEXCEPTION("Protocol for Collector \"%s\" not supported", ipAddr.c_str());
		}
	}

	SensorManager::getInstance().addSensor(this, "IpfixReceiverUdpIpV4", 0);

	msg(MSG_INFO, "UDP Receiver listening on %s:%d, FDv4=%d, FDv6=%d", (ipAddr == "")?std::string("ALL").c_str() : ipAddr.c_str(), 
								port, 
								socket4, socket6);
}


/**
 * Does UDP/IPv4 specific cleanup
 */
IpfixReceiverUdpIpV4::~IpfixReceiverUdpIpV4() {
	SensorManager::getInstance().removeSensor(this);
}


/**
 * UDP specific listener function. This function is called by @c listenerThread()
 */
void IpfixReceiverUdpIpV4::run() {
	struct sockaddr_in clientAddress4;
	struct sockaddr_in6 clientAddress6;
	socklen_t clientAddressLen4, clientAddressLen6;
	clientAddressLen4 = sizeof(struct sockaddr_in);
	clientAddressLen6 = sizeof(struct sockaddr_in6);
	
	fd_set fd_array; //all active filedescriptors
	fd_set readfds;  //parameter for for pselect

	int ret;
	struct timespec timeOut;

	FD_ZERO(&fd_array);
	if (socket4 != -1) 
		FD_SET(socket4, &fd_array);
	if (socket6 != -1)
		FD_SET(socket6, &fd_array);

	/* set a 400ms time-out on the pselect */
	timeOut.tv_sec = 0L;
	timeOut.tv_nsec = 400000000L;
	
	while(!exitFlag) {
		readfds = fd_array; // because select() changes readfds
		ret = pselect(std::max(socket4, socket6) + 1, &readfds, NULL, NULL, &timeOut, NULL);
		if (ret == 0) {
			/* Timeout */
			continue;
    		}
		if ((ret == -1) && (errno == EINTR)) {
			/* There was a signal... ignore */
			continue;
    		}
    		if (ret < 0) {
    			msg(MSG_ERROR ,"select() returned with an error");
			THROWEXCEPTION("IpfixReceiverUdpIpV4: terminating listener thread");
			break;
		}

		boost::shared_array<uint8_t> data(new uint8_t[MAX_MSG_LEN]);

		if (FD_ISSET(socket4, &readfds)) {
			ret = recvfrom(socket4, data.get(), MAX_MSG_LEN, 0, (struct sockaddr*)&clientAddress4, &clientAddressLen4);
			if (ret < 0) {
				msg(MSG_FATAL, "recvfrom from IPv4 socket returned without data, terminating listener thread");
				break;
			}
		
			if (isHostAuthorized(&clientAddress4.sin_addr, sizeof(clientAddress4.sin_addr))) {
				statReceivedPackets++;
// 				uint32_t ip = clientAddress.sin_addr.s_addr;
				boost::shared_ptr<IpfixRecord::SourceID> sourceID(new IpfixRecord::SourceID);
				memcpy(sourceID->exporterAddress.ip, &clientAddress4.sin_addr.s_addr, 4);
				sourceID->exporterAddress.len = 4;
				sourceID->exporterPort = ntohs(clientAddress4.sin_port);
				sourceID->protocol = IPFIX_protocolIdentifier_UDP;
				sourceID->receiverPort = receiverPort;
				sourceID->fileDescriptor = socket4;
				mutex.lock();
				for (std::list<IpfixPacketProcessor*>::iterator i = packetProcessors.begin(); i != packetProcessors.end(); ++i) { 
					(*i)->processPacket(data, ret, sourceID);
				}
				mutex.unlock();
			} else {
				msg(MSG_VDEBUG, "IpfixReceiverUdpIpv4: packet from unauthorized host %s discarded", inet_ntoa(clientAddress4.sin_addr));
			}
		}
		if (FD_ISSET(socket6, &readfds)) {
			ret = recvfrom(socket6, data.get(), MAX_MSG_LEN, 0, (struct sockaddr*)&clientAddress6, &clientAddressLen6);
			if (ret < 0) {
				msg(MSG_FATAL, "recvfrom from IPv6 socket returned without data, terminating listener thread");
				break;
			}
			if (isHostAuthorized(&clientAddress6.sin6_addr, sizeof(clientAddress6.sin6_addr))) {
				statReceivedPackets++;
				boost::shared_ptr<IpfixRecord::SourceID> sourceID(new IpfixRecord::SourceID);
				memcpy(sourceID->exporterAddress.ip, &clientAddress6.sin6_addr, 16);
				sourceID->exporterAddress.len = 16;
				sourceID->exporterPort = ntohs(clientAddress6.sin6_port);
				sourceID->protocol = IPFIX_protocolIdentifier_UDP;
				sourceID->receiverPort = receiverPort;
				sourceID->fileDescriptor = socket6;
				mutex.lock();
				for (std::list<IpfixPacketProcessor*>::iterator i = packetProcessors.begin(); i != packetProcessors.end(); ++i) { 
					(*i)->processPacket(data, ret, sourceID);
				}
				mutex.unlock();
			} else {
				char address[INET6_ADDRSTRLEN];
				inet_ntop(AF_INET6, &clientAddress6.sin6_addr, address, INET6_ADDRSTRLEN);
				msg(MSG_VDEBUG, "IpfixReceiverUdpIpv4: packet from unauthorized host %s discarded", address);
			}
		}
	}
	msg(MSG_DEBUG, "IpfixReceiverUdpIpV4: Exiting");
}

/**
 * statistics function called by StatisticsManager
 */
std::string IpfixReceiverUdpIpV4::getStatisticsXML(double interval)
{
	ostringstream oss;
	
	oss << "<receivedPackets>" << statReceivedPackets << "</receivedPackets>" << endl;	

	return oss.str();
}
