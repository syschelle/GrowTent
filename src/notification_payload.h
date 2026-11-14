#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
// Encode each UTF-8 byte, including &, +, =, %, quotes and line breaks.
inline String notificationFormEncode(const String& value){
  static const char hex[]="0123456789ABCDEF";
  String out;out.reserve(value.length()*3);
  for(size_t i=0;i<value.length();++i){
    const uint8_t c=static_cast<uint8_t>(value[i]);
    if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=char(c);
    else {out+='%';out+=hex[c>>4];out+=hex[c&15];}
  }
  return out;
}
inline String makePushoverPayload(const String& token,const String& user,const String& device,const String& title,const String& message){
  return String("token=")+notificationFormEncode(token)+"&user="+notificationFormEncode(user)+
    "&device="+notificationFormEncode(device)+"&title="+notificationFormEncode(title)+"&priority=1&message="+notificationFormEncode(message);
}
// ArduinoJson handles the object/number; quote every JSON control byte as well
// as quotes/backslashes, preserving embedded NUL and UTF-8 without truncation.
inline String notificationJsonQuote(const String& value){
  static const char hex[]="0123456789ABCDEF";
  String out="\"";
  for(size_t i=0;i<value.length();++i){
    const uint8_t c=static_cast<uint8_t>(value[i]);
    if(c=='"'||c=='\\'){out+='\\';out+=char(c);}
    else if(c<0x20){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
    else out+=char(c);
  }
  out+='"';return out;
}
inline String makeGotifyPayload(const String& title,const String& message,int priority){
  const String encodedTitle=notificationJsonQuote(title),encodedMessage=notificationJsonQuote(message);
  JsonDocument doc;doc["title"]=serialized(encodedTitle.c_str());doc["message"]=serialized(encodedMessage.c_str());doc["priority"]=priority;
  String body;serializeJson(doc,body);return body;
}
