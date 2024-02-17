//
// Created by Perfare on 2020/7/4.
//

#ifndef ZYGISK_IL2CPPDUMPER_HACK_H
#define ZYGISK_IL2CPPDUMPER_HACK_H

#include <stddef.h>

void hack_prepare(const char *game_data_dir, void *data, size_t length);

// The address of the pointer to decrypted global metadata
#define GlobalMetadataAddr 0x000000UL

#endif //ZYGISK_IL2CPPDUMPER_HACK_H
