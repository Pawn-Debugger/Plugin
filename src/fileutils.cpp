// Copyright (c) 2011-2015 Zeex
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include <ctime>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "fileutils.h"

namespace fileutils {

std::string GetFileName(const std::string &path) {
  std::string::size_type lastSep = path.find_last_of("/\\");
  if (lastSep != std::string::npos) {
    return path.substr(lastSep + 1);
  }
  return path;
}

std::string GetBaseName(const std::string &path) {
  std::string base = GetFileName(path);
  std::string::size_type period = base.rfind('.');
  if (period != std::string::npos) {
    base.erase(period);
  } 
  return base;
}

std::string GetExtenstion(const std::string &path) {
  std::string ext;
  std::string::size_type period = path.rfind('.');
  if (period != std::string::npos) {
    ext = path.substr(period + 1);
  } 
  return ext;
}

std::time_t GetModificationTime(const std::string &path) {
  struct stat attrib;
  if (stat(path.c_str(), &attrib) == 0) {
    return attrib.st_mtime;
  }
  return 0;
}

std::string GetRelativePath(std::string path) {
  return GetRelativePath(path, GetCurrentWorkingtDirectory());
}

std::string GetRelativePath(std::string path, const std::string &dir) {
  std::string::size_type pos = path.find(dir);
  if (pos == 0) {
    std::string::iterator begin = path.begin();
    std::string::iterator end = begin + dir.length();
    while (*end == kNativePathSepChar) {
      end++;
    }
    path.erase(begin, end);
  }
  return path;
}

} // namespace fileutils
