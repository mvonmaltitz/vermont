#include "FlowHashtable.h"
#include "IpfixRecord.hpp"
#include "crc.hpp"

#include "common/Misc.h"



FlowHashtable::FlowHashtable(Source<IpfixRecord*>* recordsource, Rule* rule, 
		uint16_t minBufferTime, uint16_t maxBufferTime)
	: BaseHashtable(recordsource, rule, minBufferTime, maxBufferTime)
{
}

FlowHashtable::~FlowHashtable()
{
}


/**
 * Adds (or otherwise aggregates) @c deltaData to @c baseData
 */
int FlowHashtable::aggregateField(IpfixRecord::FieldInfo::Type* type, IpfixRecord::Data* baseData, IpfixRecord::Data* deltaData) {
	switch (type->id) {

		case IPFIX_TYPEID_flowStartSysUpTime:
		case IPFIX_TYPEID_flowStartSeconds:
		case IPFIX_TYPEID_flowStartMicroSeconds:
		case IPFIX_TYPEID_flowStartNanoSeconds:
			if (type->length != 4) {
				DPRINTF("unsupported length %d for type %d", type->length, type->id);
				goto out;
			}

			*(uint32_t*)baseData = lesserUint32Nbo(*(uint32_t*)baseData, *(uint32_t*)deltaData);
			break;

		case IPFIX_TYPEID_flowStartMilliSeconds:
			if (type->length != 8) {
				DPRINTF("unsupported length %d for type %d", type->length, type->id);
				goto out;
			}

			*(uint64_t*)baseData = lesserUint64Nbo(*(uint64_t*)baseData, *(uint64_t*)deltaData);
			break;

		case IPFIX_TYPEID_flowEndSysUpTime:
		case IPFIX_TYPEID_flowEndSeconds:
		case IPFIX_TYPEID_flowEndMicroSeconds:
		case IPFIX_TYPEID_flowEndNanoSeconds:
			if (type->length != 4) {
				DPRINTF("unsupported length %d for type %d", type->length, type->id);
				goto out;
			}

			*(uint32_t*)baseData = greaterUint32Nbo(*(uint32_t*)baseData, *(uint32_t*)deltaData);
			break;

		case IPFIX_TYPEID_flowEndMilliSeconds:
			if (type->length != 8) {
				DPRINTF("unsupported length %d for type %d", type->length, type->id);
				goto out;
			}

			*(uint64_t*)baseData = greaterUint64Nbo(*(uint64_t*)baseData, *(uint64_t*)deltaData);
			break;

		case IPFIX_TYPEID_octetDeltaCount:
		case IPFIX_TYPEID_postOctetDeltaCount:
		case IPFIX_TYPEID_packetDeltaCount:
		case IPFIX_TYPEID_postPacketDeltaCount:
		case IPFIX_TYPEID_droppedOctetDeltaCount:
		case IPFIX_TYPEID_droppedPacketDeltaCount:
			// TODO: tobi_optimize
			// converting all values to network byte order when sending ipfix packets would be much faster
			switch (type->length) {
				case 1:
					*(uint8_t*)baseData = addUint8Nbo(*(uint8_t*)baseData, *(uint8_t*)deltaData);
					return 0;
				case 2:
					*(uint16_t*)baseData = addUint16Nbo(*(uint16_t*)baseData, *(uint16_t*)deltaData);
					return 0;
				case 4:
					*(uint32_t*)baseData = addUint32Nbo(*(uint32_t*)baseData, *(uint32_t*)deltaData);
					return 0;
				case 8:
					*(uint64_t*)baseData = addUint64Nbo(*(uint64_t*)baseData, *(uint64_t*)deltaData);
					return 0;
				default:
					DPRINTF("unsupported length %d for type %d", type->length, type->id);
					goto out;
			}
			break;

		default:
			DPRINTF("non-aggregatable type: %d", type->id);
			goto out;
			break;
	}

	return 0;
	out:
	return 1;
}


/**
 * Adds (or otherwise aggregates) pertinent fields of @c flow to @c baseFlow
 */
int FlowHashtable::aggregateFlow(IpfixRecord::Data* baseFlow, IpfixRecord::Data* flow)
{
	int i;

	if(!baseFlow) {
		DPRINTF("aggregateFlow: baseFlow is NULL");
		return 1;
	}

	if(!flow){
		DPRINTF("aggregateFlow: flow is NULL");
		return 1;
	}

	for (i = 0; i < dataTemplate->fieldCount; i++) {
		IpfixRecord::FieldInfo* fi = &dataTemplate->fieldInfo[i];

		if(!isToBeAggregated(fi->type)) {
			continue;
		}
		aggregateField(&fi->type, baseFlow + fi->offset, flow + fi->offset);
	}

	return 0;
}


/**
 * Returns a hash value corresponding to all variable, non-aggregatable fields of a flow
 */
uint16_t FlowHashtable::getHash(IpfixRecord::Data* data) {
	int i;

	uint32_t hash = 0;
	for (i = 0; i < dataTemplate->fieldCount; i++) {
		if(isToBeAggregated(dataTemplate->fieldInfo[i].type)) {
			continue;
		}
		hash = crc32(hash,
				dataTemplate->fieldInfo[i].type.length,
				(char*)data + dataTemplate->fieldInfo[i].offset
		);
	}

	return hash & HTABLE_SIZE-1;
}

/**
 * Checks if two data fields are binary equal
 * @return 1 if fields are equal
 */
int FlowHashtable::equalRaw(IpfixRecord::FieldInfo::Type* data1Type, IpfixRecord::Data* data1, IpfixRecord::FieldInfo::Type* data2Type, IpfixRecord::Data* data2) 
{
	int i;

	if(data1Type->id != data2Type->id) return 0;
	if(data1Type->length != data2Type->length) return 0;
	if(data1Type->eid != data2Type->eid) return 0;

	for(i = 0; i < data1Type->length; i++) {
		if(data1[i] != data2[i]) {
			return 0;
		}
	}

	return 1;
}

/**
 * Checks if all of two flows' (non-aggregatable) fields are binary equal
 * @return 1 if fields are equal
 */
int FlowHashtable::equalFlow(IpfixRecord::Data* flow1, IpfixRecord::Data* flow2)
{
	int i;

	if(flow1 == flow2) return 1;

	for(i = 0; i < dataTemplate->fieldCount; i++) {
		IpfixRecord::FieldInfo* fi = &dataTemplate->fieldInfo[i];

		if(isToBeAggregated(fi->type)) {
			continue;
		}

		if(!equalRaw(&fi->type, flow1 + fi->offset, &fi->type, flow2 + fi->offset)) {
			return 0;
		}
	}

	return 1;
}

/**
 * Inserts a data block into the hashtable
 */
void FlowHashtable::bufferDataBlock(boost::shared_array<IpfixRecord::Data> data)
{
	statRecordsReceived++;

	uint16_t hash = getHash(data.get());
	Bucket* bucket = buckets[hash];

	if (bucket == 0) {
		/* This slot is still free, place the bucket here */
		DPRINTF("bufferDataBlock: creating bucket\n");
		buckets[hash] = createBucket(data);
		return;
	}

	/* This slot is already used, search spill chain for equal flow */
	while(1) {
		if (equalFlow(bucket->data.get(), data.get())) {
			DPRINTF("appending to bucket\n");

			aggregateFlow(bucket->data.get(), data.get());
			// TODO: tobi_optimize
			// replace call of time() with access to a static variable which is updated regularly (such as every 100ms)
			bucket->expireTime = time(0) + minBufferTime;

			break;
		}

		if (bucket->next == 0) {
			DPRINTF("creating bucket\n");

			bucket->next = createBucket(data);
			break;
		}
		bucket = (Bucket*)bucket->next;
	}
}

/**
 * Copies \c srcData to \c dstData applying \c modifier.
 * Takes care to pad \c srcData with zero-bytes in case it is shorter than \c dstData.
 */
void FlowHashtable::copyData(IpfixRecord::FieldInfo::Type* dstType, IpfixRecord::Data* dstData, IpfixRecord::FieldInfo::Type* srcType, IpfixRecord::Data* srcData, Rule::Field::Modifier modifier)
{
	if((dstType->id != srcType->id) || (dstType->eid != srcType->eid)) {
		DPRINTF("copyData: Tried to copy field to destination of different type\n");
		return;
	}

	/* Copy data, care for length differences */
	if(dstType->length == srcType->length) {
		memcpy(dstData, srcData, srcType->length);

	} else if(dstType->length > srcType->length) {

		/* TODO: We simply pad with zeroes - will this always be correct? */
		if((dstType->id == IPFIX_TYPEID_sourceIPv4Address) || (dstType->id == IPFIX_TYPEID_destinationIPv4Address)) {
			/* Fields of type IPv4Address-type are padded on the right */
			bzero(dstData, dstType->length);
			memcpy(dstData, srcData, srcType->length);
		} else {
			/* TODO: all other types are padded on the left, i.e. the "big" end */
			bzero(dstData, dstType->length);
			memcpy(dstData + dstType->length - srcType->length, srcData, srcType->length);
		}

	} else {
		DPRINTF("Target buffer too small. Buffer expected %s of length %d, got one with length %dn", typeid2string(dstType->id), srcType->length, dstType->length);
		return;
	}

	/* Apply modifier */
	if(modifier == Rule::Field::DISCARD) {
		DPRINTF("Tried to copy data w/ having field modifier set to discard\n");
		return;
	} else if((modifier == Rule::Field::KEEP) || (modifier == Rule::Field::AGGREGATE)) {

	} else if((modifier >= Rule::Field::MASK_START) && (modifier <= Rule::Field::MASK_END)) {

		if((dstType->id != IPFIX_TYPEID_sourceIPv4Address) && (dstType->id != IPFIX_TYPEID_destinationIPv4Address)) {
			DPRINTF("Tried to apply mask to %s field\n", typeid2string(dstType->id));
			return;
		}

		if (dstType->length != 5) {
			DPRINTF("Destination data to short - no room to store mask\n");
			return;
		}

		uint8_t imask = 32 - (modifier - (int)Rule::Field::MASK_START);
		dstData[4] = imask; /* store the inverse network mask */

		if (imask > 0) {
			if (imask == 8) {
				dstData[3] = 0x00;
			} else if (imask == 16) {
				dstData[2] = 0x00;
				dstData[3] = 0x00;
			} else if (imask == 24) {
				dstData[1] = 0x00;
				dstData[2] = 0x00;
				dstData[3] = 0x00;
			} else if (imask == 32) {
				dstData[0] = 0x00;
				dstData[1] = 0x00;
				dstData[2] = 0x00;
				dstData[3] = 0x00;
			} else {
				int pattern = 0;
				int i;
				for(i = 0; i < imask; i++) {
					pattern |= (1 << i);
				}

				*(uint32_t*)dstData = htonl(ntohl(*(uint32_t*)(dstData)) & ~pattern);
			}
		}

	} else {
		DPRINTF("Unhandled field modifier: %d\n", modifier);
		return;
	}
}


/**
 * Buffer passed flow in Hashtable @c ht
 */
void FlowHashtable::aggregateTemplateData(IpfixRecord::TemplateInfo* ti, IpfixRecord::Data* data)
{
	DPRINTF("FlowHashtable::aggregateTemplateData called");
	int i;

	/* Create data block to be inserted into buffer... */
	boost::shared_array<IpfixRecord::Data> htdata(new IpfixRecord::Data[fieldLength]);

	for (i = 0; i < dataTemplate->fieldCount; i++) {
		IpfixRecord::FieldInfo* hfi = &dataTemplate->fieldInfo[i];
		IpfixRecord::FieldInfo* tfi = ti->getFieldInfo(&hfi->type);

		if(!tfi) {
			DPRINTF("Flow to be buffered did not contain %s field\n", typeid2string(hfi->type.id));
			continue;
		}

		DPRINTFL(MSG_VDEBUG, "copyData for type %d, offset %x, starting from pointer %X", tfi->type.id, tfi->offset, data+tfi->offset);
		DPRINTFL(MSG_VDEBUG, "copyData to offset %X", hfi->offset);
		copyData(&hfi->type, htdata.get() + hfi->offset, &tfi->type, data + tfi->offset, fieldModifier[i]);

		/* copy associated mask, should there be one */
		switch (hfi->type.id) {
			case IPFIX_TYPEID_sourceIPv4Address:
				tfi = ti->getFieldInfo(IPFIX_TYPEID_sourceIPv4Mask, 0);

				if(tfi) {
					if(hfi->type.length != 5) {
						DPRINTF("Tried to set mask of length %d IP address\n", hfi->type.length);
					} else {
						if(tfi->type.length == 1) {
							*(uint8_t*)(htdata.get() + hfi->offset + 4) = *(uint8_t*)(data + tfi->offset);
						} else {
							DPRINTF("Cannot process associated mask with invalid length %d\n", tfi->type.length);
						}
					}
				}
				break;

			case IPFIX_TYPEID_destinationIPv4Address:
				tfi = ti->getFieldInfo(IPFIX_TYPEID_destinationIPv4Mask, 0);

				if(tfi) {
					if(hfi->type.length != 5) {
						DPRINTF("Tried to set mask of length %d IP address\n", hfi->type.length);
					} else {
						if(tfi->type.length == 1) {
							*(uint8_t*)(htdata.get() + hfi->offset + 4) = *(uint8_t*)(data + tfi->offset);
						} else {
							DPRINTF("Cannot process associated mask with invalid length %d\n", tfi->type.length);
						}
					}
				}
				break;

			default:
				break;
		}
	}

	/* ...then buffer it */
	bufferDataBlock(htdata);
}


/**
 * Buffer passed flow (containing fixed-value fields) in Hashtable @c ht
 */
void FlowHashtable::aggregateDataTemplateData(IpfixRecord::DataTemplateInfo* ti, IpfixRecord::Data* data)
{
	DPRINTF("called");
	int i;

	/* Create data block to be inserted into buffer... */
	boost::shared_array<IpfixRecord::Data> htdata(new IpfixRecord::Data[fieldLength]);

	for (i = 0; i < dataTemplate->fieldCount; i++) {
		IpfixRecord::FieldInfo* hfi = &dataTemplate->fieldInfo[i];

		/* Copy from matching variable field, should it exist */
		IpfixRecord::FieldInfo* tfi = ti->getFieldInfo(&hfi->type);
		if(tfi) {
			copyData(&hfi->type, htdata.get() + hfi->offset, &tfi->type, data + tfi->offset, fieldModifier[i]);

			/* copy associated mask, should there be one */
			switch (hfi->type.id) {

				case IPFIX_TYPEID_sourceIPv4Address:
					tfi = ti->getFieldInfo(IPFIX_TYPEID_sourceIPv4Mask, 0);
					if(tfi) {
						if(hfi->type.length != 5) {
							DPRINTF("Tried to set mask of length %d IP address\n", hfi->type.length);
						} else {
							if(tfi->type.length == 1) {
								*(uint8_t*)(htdata.get() + hfi->offset + 4) = *(uint8_t*)(data + tfi->offset);
							} else {
								DPRINTF("Cannot process associated mask with invalid length %d\n", tfi->type.length);
							}
						}
					}
					break;

				case IPFIX_TYPEID_destinationIPv4Address:
					tfi = ti->getFieldInfo(IPFIX_TYPEID_destinationIPv4Mask, 0);
					if(tfi) {
						if(hfi->type.length != 5) {
							DPRINTF("Tried to set mask of length %d IP address", hfi->type.length);
						} else {
							if(tfi->type.length == 1) {
								*(uint8_t*)(htdata.get() + hfi->offset + 4) = *(uint8_t*)(data + tfi->offset);
							} else {
								DPRINTF("Cannot process associated mask with invalid length %d", tfi->type.length);
							}
						}
					}
					break;

				default:
					break;
			}

			continue;
		}

		/* No matching variable field. Copy from matching fixed field, should it exist */
		tfi = ti->getDataInfo(&hfi->type);
		if(tfi) {
			copyData(&hfi->type, htdata.get() + hfi->offset, &tfi->type, ti->data + tfi->offset, fieldModifier[i]);

			/* copy associated mask, should there be one */
			switch (hfi->type.id) {

				case IPFIX_TYPEID_sourceIPv4Address:
					tfi = ti->getDataInfo(IPFIX_TYPEID_sourceIPv4Mask, 0);
					if(tfi) {
						if(hfi->type.length != 5) {
							DPRINTF("Tried to set mask of length %d IP address\n", hfi->type.length);
						} else {
							if(tfi->type.length == 1) {
								*(uint8_t*)(htdata.get() + hfi->offset + 4) = *(uint8_t*)(ti->data + tfi->offset);
							} else {
								DPRINTF("Cannot process associated mask with invalid length %d\n", tfi->type.length);
							}
						}
					}
					break;

				case IPFIX_TYPEID_destinationIPv4Address:
					tfi = ti->getDataInfo(IPFIX_TYPEID_destinationIPv4Mask, 0);
					if(tfi) {
						if(hfi->type.length != 5) {
							DPRINTF("Tried to set mask of length %d IP address\n", hfi->type.length);
						} else {
							if (tfi->type.length == 1) {
								*(uint8_t*)(htdata.get() + hfi->offset + 4) = *(uint8_t*)(ti->data + tfi->offset);
							} else {
								DPRINTF("Cannot process associated mask with invalid length %d\n", tfi->type.length);
							}
						}
					}
					break;

				default:
					break;
			}
			continue;
		}

		msg(MSG_FATAL, "Flow to be buffered did not contain %s field", typeid2string(hfi->type.id));
		continue;
	}

	/* ...then buffer it */
	bufferDataBlock(htdata);
}