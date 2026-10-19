#pragma once
#include <set>
#include <string>
#include <iostream>
// Actual LVGL layout after binding values. App fixtures use this, not the
// renderer admission path: ordinary applications may intentionally clip content.
static std::set<std::string> suiteLayoutErrors;
static void checkSuiteLayout(lv_obj_t*node,const char*path){
 if(lv_obj_has_flag(node,LV_OBJ_FLAG_HIDDEN))return;
 const bool label=lv_obj_check_type(node,&lv_label_class);
 if(label&&strlen(lv_label_get_text(node))){
  auto parent=lv_obj_get_parent(node);lv_area_t a,b;lv_obj_get_coords(node,&a);lv_obj_get_coords(parent,&b);
  auto fail=[&](const char*reason){std::string key=std::string(path)+": "+reason+" ["+lv_label_get_text(node)+"]";if(suiteLayoutErrors.insert(key).second)std::cerr<<key<<" box="<<a.x1<<","<<a.y1<<".."<<a.x2<<","<<a.y2<<" parent="<<b.x1<<","<<b.y1<<".."<<b.x2<<","<<b.y2<<"\n";};
  if(a.x1<b.x1||a.y1<b.y1||a.x2>b.x2||a.y2>b.y2)fail("text outside parent");
  for(unsigned i=0;i<lv_obj_get_child_count(parent);++i){auto sibling=lv_obj_get_child(parent,i);if(sibling==node||lv_obj_has_flag(sibling,LV_OBJ_FLAG_HIDDEN))continue;
   if(lv_obj_check_type(sibling,&lv_button_class)){lv_area_t target;lv_obj_get_coords(sibling,&target);if(a.x1<=target.x2&&a.x2>=target.x1&&a.y1<=target.y2&&a.y2>=target.y1)fail("text overlaps a sibling button");}
  }
  if(lv_label_get_long_mode(node)==LV_LABEL_LONG_WRAP){
   lv_point_t size;lv_text_get_size(&size,lv_label_get_text(node),lv_obj_get_style_text_font(node,0),lv_obj_get_style_text_letter_space(node,0),lv_obj_get_style_text_line_space(node,0),lv_obj_get_content_width(node),LV_TEXT_FLAG_NONE);
   if(size.y>lv_obj_get_content_height(node)){std::cerr<<"required="<<size.y<<" available="<<lv_obj_get_content_height(node)<<" font="<<lv_obj_get_style_text_font(node,0)->line_height<<" spacing="<<lv_obj_get_style_text_line_space(node,0)<<" width="<<lv_obj_get_content_width(node)<<"\n";fail("wrapped text clipped vertically");}
  }
 }
 for(unsigned i=0;i<lv_obj_get_child_count(node);++i)checkSuiteLayout(lv_obj_get_child(node,i),path);
}
