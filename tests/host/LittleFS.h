#pragma once
#include <fstream>
#include <memory>
#include <filesystem>
#include <cstdlib>
#include <cstdint>
class File {
 struct Handle {std::fstream stream;size_t length=0;};std::shared_ptr<Handle> h;
public:
 File()=default;File(const std::string&p,const char*mode){h=std::make_shared<Handle>();h->stream.open(p,std::ios::binary|(*mode=='w'?(std::ios::out|std::ios::trunc):std::ios::in));if(h->stream&&*mode!='w')h->length=std::filesystem::file_size(p);}
 explicit operator bool()const{return h&&h->stream.is_open();}
 size_t size(){return h?h->length:0;}size_t position(){return h?size_t(h->stream.tellg()):0;}
 bool seek(size_t n){if(!h)return false;h->stream.clear();h->stream.seekg(n);return !h->stream.fail();}
 size_t read(uint8_t *p,size_t n){if(!h)return 0;h->stream.read((char*)p,n);return size_t(h->stream.gcount());}
 size_t write(const uint8_t*p,size_t n){if(!h)return 0;h->stream.write((const char*)p,n);if(h->stream.fail())return 0;h->length+=n;return n;}
 void flush(){if(h)h->stream.flush();}void close(){if(h)h->stream.close();h.reset();}
};
struct HostLittleFS {
 std::string path(const char*p){const char*root=std::getenv("NOTES_TEST_FS");return std::string(root?root:"build/host-fs")+p;}
 bool begin(bool){return true;}File open(const char*p,const char*mode){return File(path(p),mode);}
 bool exists(const char*p){return std::filesystem::exists(path(p));}bool remove(const char*p){return std::filesystem::remove(path(p));}bool rename(const char*a,const char*b){std::error_code ec;std::filesystem::rename(path(a),path(b),ec);return !ec;}
};
inline HostLittleFS LittleFS;
