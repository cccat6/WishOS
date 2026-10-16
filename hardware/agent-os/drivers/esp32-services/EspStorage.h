#pragma once
#include <Arduino.h>
#include "Core.h"
#include "Vfs.h"
#include <esp_partition.h>
namespace notesos {
inline const char*describeEspStorage(Vfs&vfs,JsonObjectConst args,JsonObject out){
 if(auto error=argumentFields(args,"cursor,limit,partition_cursor,partition_limit"))return error;
 for(auto key:{"cursor","limit","partition_cursor","partition_limit"})if(args.containsKey(key)&&!args[key].is<uint32_t>())return "storage_cursor_type";
 unsigned cursor=args["cursor"]|0u,limit=args["limit"]|4u,partitionCursor=args["partition_cursor"]|0u,partitionLimit=args["partition_limit"]|4u;
 if(!limit||limit>8||!partitionLimit||partitionLimit>8)return "storage_page_limit";
 auto providers=out["providers"].to<JsonArray>();vfs.describe(providers,cursor,limit);out["next_cursor"]=cursor+providers.size();out["more"]=cursor+providers.size()<vfs.count();
 out["internal_heap_free"]=ESP.getFreeHeap();out["psram_physical_bytes"]=ESP.getPsramSize();out["psram_free"]=ESP.getFreePsram();out["flash_physical_bytes"]=ESP.getFlashChipSize();out["firmware_bytes"]=ESP.getSketchSize();out["partition_resize_supported"]=false;
 auto list=out["flash_partitions"].to<JsonArray>();auto it=esp_partition_find(ESP_PARTITION_TYPE_ANY,ESP_PARTITION_SUBTYPE_ANY,nullptr);unsigned seen=0;
 while(it){auto part=esp_partition_get(it);if(seen++>=partitionCursor&&list.size()<partitionLimit){auto item=list.add<JsonObject>();item["label"]=part->label;item["type"]=int(part->type);item["subtype"]=int(part->subtype);sizeText(item,"offset",part->address);sizeText(item,"capacity_bytes",part->size);item["encrypted"]=part->encrypted;}it=esp_partition_next(it);}
 out["partition_next_cursor"]=partitionCursor+list.size();out["partition_more"]=partitionCursor+list.size()<seen;return nullptr;
}
}
