#define NOMINMAX
#include "RuntimeUI.h"
#include <stdexcept>
#include <algorithm>
#ifdef _WIN32
#include <Windows.h>
#endif
namespace ui {
std::vector<unsigned> DecodeUTF8(const std::string& text){std::vector<unsigned> result;
 for(size_t i=0;i<text.size();){unsigned a=static_cast<unsigned char>(text[i++]);if(a<128){result.push_back(a);continue;}unsigned count=a>=0xc2&&a<=0xdf?1:a>=0xe0&&a<=0xef?2:a>=0xf0&&a<=0xf4?3:0;unsigned value=a&((1u<<(6-count))-1);if(!count||i+count>text.size()){result.push_back(0xfffd);continue;}bool valid=true;for(unsigned n=0;n<count;++n){unsigned b=static_cast<unsigned char>(text[i+n]);if((b&0xc0)!=0x80){valid=false;break;}value=(value<<6)|(b&63);}if(!valid){result.push_back(0xfffd);continue;}i+=count;if(value>0x10ffff||(value>=0xd800&&value<=0xdfff)||(count==1&&value<128)||(count==2&&value<2048)||(count==3&&value<65536))value=0xfffd;result.push_back(value);
 }return result;
}
FontAtlas RasterizeFont(){FontAtlas atlas;
#ifdef _WIN32
 HDC dc=CreateCompatibleDC(nullptr);if(!dc)throw std::runtime_error("Runtime UI font DC failed");HFONT font=CreateFontW(-32,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");if(!font){DeleteDC(dc);throw std::runtime_error("Runtime UI font creation failed");}auto old=SelectObject(dc,font);TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);atlas.ascent=float(metrics.tmAscent);atlas.descent=float(metrics.tmDescent);atlas.lineHeight=float(metrics.tmHeight);atlas.texture.width=1024;atlas.texture.height=1024;atlas.texture.pixels.resize(1024*1024*4,0);for(size_t i=0;i<atlas.texture.pixels.size();i+=4)atlas.texture.pixels[i]=atlas.texture.pixels[i+1]=atlas.texture.pixels[i+2]=255;atlas.texture.pixels[3]=255;
 MAT2 identity{};identity.eM11.value=1;identity.eM22.value=1;int x=2,y=2,rowHeight=0;std::vector<unsigned> points;for(unsigned c=32;c<127;++c)points.push_back(c);for(unsigned c=0x400;c<=0x45f;++c)points.push_back(c);
 for(auto c:FontCoverage())if(std::find(points.begin(),points.end(),c)==points.end())points.push_back(c);
 bool failed=false;
 for(unsigned c:points){WCHAR ch=WCHAR(c);WORD index=0;if(GetGlyphIndicesW(dc,&ch,1,&index,GGI_MARK_NONEXISTING_GLYPHS)==GDI_ERROR||index==0xffff)continue;GLYPHMETRICS gm{};DWORD count=GetGlyphOutlineW(dc,c,GGO_GRAY8_BITMAP,&gm,0,nullptr,&identity);if(count==GDI_ERROR){continue;}if(count==0){gm.gmBlackBoxX=gm.gmBlackBoxY=0;}if(x+int(gm.gmBlackBoxX)+1>=1024){x=2;y+=rowHeight+2;rowHeight=0;}if(y+gm.gmBlackBoxY+1>=1024){failed=true;break;}std::vector<unsigned char> bytes(count);if(count&&GetGlyphOutlineW(dc,c,GGO_GRAY8_BITMAP,&gm,count,bytes.data(),&identity)==GDI_ERROR){failed=true;continue;}
  unsigned stride=(gm.gmBlackBoxX+3)&~3u;if(size_t(stride)*gm.gmBlackBoxY>bytes.size()){failed=true;continue;}for(unsigned row=0;row<gm.gmBlackBoxY;++row)for(unsigned col=0;col<gm.gmBlackBoxX;++col)atlas.texture.pixels[((y+row)*1024+x+col)*4+3]=static_cast<unsigned char>(std::min(255u,unsigned(bytes[row*stride+col])*255/64));
  Glyph g;g.pixels={float(gm.gmptGlyphOrigin.x),float(-gm.gmptGlyphOrigin.y),float(gm.gmBlackBoxX),float(gm.gmBlackBoxY)};g.advance=float(gm.gmCellIncX);g.uv={x/1024.f,y/1024.f,(x+gm.gmBlackBoxX)/1024.f,(y+gm.gmBlackBoxY)/1024.f};atlas.glyphs[c]=g;x+=gm.gmBlackBoxX+2;rowHeight=std::max(rowHeight,int(gm.gmBlackBoxY));
 }
 SelectObject(dc,old);DeleteObject(font);DeleteDC(dc);if(failed||!atlas.glyphs.contains(0x42f)||!atlas.glyphs.contains('?'))throw std::runtime_error("Runtime UI font atlas lacks required Cyrillic glyphs");
#else
 throw std::runtime_error("Runtime UI font rasterization currently requires Windows");
#endif
 return atlas;
}
}
