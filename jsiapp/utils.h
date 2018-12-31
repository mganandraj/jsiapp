#pragma once

#include <string>

char ToLower(char c);

std::string ToLower(const std::string& in);

bool StringEqualNoCase(const char* a, const char* b);

bool StringEqualNoCaseN(const char* a, const char* b, size_t length);