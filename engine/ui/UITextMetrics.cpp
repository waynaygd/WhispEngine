#include "UIFont.h"
#include <cmath>
#include <limits>
namespace ui {
std::pair<std::string,float> MeasureText(const FontAtlas& font,const std::string& text,float size,float width){
 if(!ValidUTF8(text)||text.size()>16384||!std::isfinite(size)||!std::isfinite(width)||size<1||size>256||width<1||width>16384)throw std::runtime_error("UI: invalid text measurement");
 auto points=DecodeUTF8(text);std::string out;unsigned lines=0;const float unit=size/font.height;
 auto encode=[&](unsigned c){if(c<128)out+=char(c);else if(c<2048){out+=char(192|(c>>6));out+=char(128|(c&63));}else if(c<65536){out+=char(224|(c>>12));out+=char(128|((c>>6)&63));out+=char(128|(c&63));}else{out+=char(240|(c>>18));out+=char(128|((c>>12)&63));out+=char(128|((c>>6)&63));out+=char(128|(c&63));}};
 for(std::size_t start=0;start<points.size();){
  std::size_t end=start,space=std::numeric_limits<std::size_t>::max();float advance=0;
  for(;end<points.size()&&points[end]!='\n';++end){auto c=points[end];if(c=='\r')continue;auto glyph=font.glyphs.find(c);if(glyph==font.glyphs.end())glyph=font.glyphs.find('?');float next=glyph==font.glyphs.end()?0:glyph->second.advance*unit;
   if(advance+next>width&&end>start){if(space!=std::numeric_limits<std::size_t>::max()&&space>start)end=space;break;}if(c==' ')space=end;advance+=next;
  }
  if(lines++)out+='\n';for(auto n=start;n<end;++n)if(points[n]!='\r')encode(points[n]);
  start=end;if(start<points.size()&&points[start]=='\n')++start;else while(start<points.size()&&points[start]==' ')++start;
 }
 return {out,std::max(1u,lines)*font.lineHeight*unit+4};
}
}
