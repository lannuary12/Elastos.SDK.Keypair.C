// Copyright (c) 2012-2018 The Elastos Open Source Project
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef __BASE64_H__
#define __BASE64_H__

#include <string>

#include <CMemBlock.h>

class Base64 {
public:
    static std::vector<unsigned char> toBits(const std::string &base64Str);

    static std::string fromBits(const unsigned char *bits, size_t length);
};


#endif //__BASE64_H__
