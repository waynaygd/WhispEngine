#pragma once
#include "../ui/RuntimeUI.h"
#include <set>
namespace editor::widgets {
const ui::Widget& Find(const ui::Document&,const std::string&);
ui::Rect Canvas(const ui::Document&,ui::Pair preview);
ui::Rect Rectangle(const ui::Document&,const std::string&,ui::Rect canvas);
ui::Rect ParentRectangle(const ui::Document&,const ui::Widget&,ui::Rect canvas);
void SetRectangle(ui::Widget&,ui::Rect rectangle,ui::Rect parent);
void Reparent(ui::Document&,const std::string& id,const std::string& parent,ui::Rect canvas);
enum class Drop { Before, Inside, After };
void Move(ui::Document&,const std::string& id,const std::string& target,Drop,ui::Rect canvas);
void Rename(ui::Document&,const std::string& id,const std::string& name);
std::string Duplicate(ui::Document&,const std::string& id);
void Delete(ui::Document&,const std::string& id);
void Preset(ui::Document&,const std::string& id,int anchor,int pivot,ui::Rect canvas);
ui::Rect Drag(ui::Rect start,float dx,float dy,int handle,float grid);
std::string Pick(const ui::DrawList&,float x,float y,const std::set<std::string>& locked={});
}
