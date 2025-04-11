//
// Created by Perfare on 2020/7/4.
//

#ifndef ZYGISK_IL2CPPDUMPER_HACK_H
#define ZYGISK_IL2CPPDUMPER_HACK_H

#include <stddef.h>

void hack_prepare(const char *game_data_dir, void *data, size_t length);

// The address of the pointer to decrypted global metadata
#define GlobalMetadataAddr 0xA67D770UL

#define CodeRegAddr 0xA67D758UL
#define MetaRegAddr 0xA67D760UL
#define CodeGenOptAddr 0xA67D768UL

#define GetChatMaskFuncAddr 0x37E5404UL

#endif //ZYGISK_IL2CPPDUMPER_HACK_H
