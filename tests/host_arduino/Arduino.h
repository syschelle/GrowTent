#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <type_traits>
#include <cstdio>
class String:public std::string {
public:
 using std::string::string;using std::string::operator=;
 String()=default;String(const std::string& v):std::string(v){}
 template<class T,typename std::enable_if<std::is_integral<T>::value,int>::type=0> explicit String(T n):std::string(std::to_string(n)){}
 String(double n,unsigned int decimals){char b[96];std::snprintf(b,sizeof(b),"%.*f",int(decimals),n);assign(b);}
 bool isEmpty()const{return empty();}
 size_t write(uint8_t c){push_back(char(c));return 1;}
 size_t write(const uint8_t* p,size_t n){append(reinterpret_cast<const char*>(p),n);return n;}
};
uint32_t millis();
