#include "Query.hxx"

namespace tinyhtm
{
  int64_t Query::callback(int (*fn)(void *entry, int num_elements,
                                    hid_t *types,
                                    char **names)) const
  {
    int64_t count;
    enum htm_errcode ec;
    if(type==Type::circle)
      count=htm_tree_s2circle_callback(&(tree.tree), &(center.v3), r, &ec, fn);
    else if(type==Type::ellipse)
      count=htm_tree_s2ellipse_callback(&(tree.tree), &(ellipse.ellipse), &ec, fn);
    else if(type==Type::polygon)
      count=htm_tree_s2cpoly_callback(&(tree.tree), poly, &ec, fn);
    else
      throw Exception("Bad tinyhtm::Query::Type");
    return count;
  }
}
