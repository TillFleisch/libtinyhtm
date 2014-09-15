#include "_htm_ids_init.hxx"
#include "_htm_ids_add.hxx"
#include "_htm_simplify_ids.hxx"
#include "htm/_htm_s2circle_htmcov.hxx"

extern "C" {

struct htm_ids * htm_s2circle_ids(struct htm_ids *ids,
                                  const struct htm_v3 *center,
                                  double radius,
                                  int level,
                                  size_t maxranges,
                                  enum htm_errcode *err)
{
    struct _htm_path path;
    double dist2;
    int efflevel;

    if (center == NULL) {
        if (err != NULL) {
            *err = HTM_ENULLPTR;
        }
        free(ids);
        return NULL;
    } else if (level < 0 || level > HTM_MAX_LEVEL) {
        if (err != NULL) {
            *err = HTM_ELEVEL;
        }
        free(ids);
        return NULL;
    }
    if (ids == NULL) {
        ids = _htm_ids_init();
        if (ids == NULL) {
            if (err != NULL) {
                *err = HTM_ENOMEM;
            }
            return NULL;
        }
    } else {
        ids->n = 0;
    }
    /* Deal with degenerate cases */
    if (radius < 0.0) {
        /* empty ID list */
        if (err != NULL) {
            *err = HTM_OK;
        }
        return ids;
    } else if (radius >= 180.0) {
        /* the entire sky */
        int64_t min_id = (8 + HTM_S0) << level * 2;
        int64_t max_id = ((8 + HTM_NROOTS) << level * 2) - 1;
        if (err != NULL) {
            *err = HTM_OK;
        }
        ids = _htm_ids_add(ids, min_id, max_id);
        if (ids == NULL && err != NULL) {
            *err = HTM_ENOMEM;
        }
        return ids;
    }

    efflevel = level;
    /* compute square of secant distance corresponding to radius */
    dist2 = sin(radius * 0.5 * HTM_RAD_PER_DEG);
    dist2 = 4.0 * dist2 * dist2;

    for (int root = HTM_S0; root <= HTM_N3; ++root) {
        struct _htm_node *curnode = path.node;
        int curlevel = 0;
        _htm_path_root(&path, static_cast<htm_root>(root));

        while (1) {
            switch (_htm_s2circle_htmcov(curnode, center, dist2)) {
                case HTM_CONTAINS:
                    if (curlevel == 0) {
                        /* no need to consider other roots */
                        root = HTM_N3;
                    } else {
                        /* no need to consider other children of parent */
                        curnode[-1].child = 4;
                    }
                    /* fall-through */
                case HTM_INTERSECT:
                    if (curlevel < efflevel) {
                        /* continue subdividing */
                        _htm_node_prep0(curnode);
                        _htm_node_make0(curnode);
                        ++curnode;
                        ++curlevel;
                        continue;
                    }
                    /* fall-through */
                case HTM_INSIDE:
                    /* reached a leaf or fully covered HTM triangle,
                       append HTM ID range to results */
                    {
                        int64_t id = curnode->id << (level - curlevel) * 2;
                        int64_t n = ((int64_t) 1) << (level - curlevel) * 2;
                        ids = _htm_ids_add(ids, id, id + n - 1);
                    }
                    if (ids == NULL) {
                        if (err != NULL) {
                            *err = HTM_ENOMEM;
                        }
                        return NULL;
                    }
                    while (ids->n > maxranges && efflevel != 0) {
                        /* too many ranges:
                           reduce effective subdivision level */
                        --efflevel;
                        if (curlevel > efflevel) {
                           curnode = curnode - (curlevel - efflevel);
                           curlevel = efflevel;
                        }
                        _htm_simplify_ids(ids, level - efflevel);
                    }
                    break;
                default:
                    /* HTM triangle does not intersect circle */
                    break;
            }
            /* ascend towards the root */
            --curlevel;
            --curnode;
            while (curlevel >= 0 && curnode->child == 4) {
                --curnode;
                --curlevel;
            }
            if (curlevel < 0) {
                /* finished with this root */
                break;
            }
            if (curnode->child == 1) {
                _htm_node_prep1(curnode);
                _htm_node_make1(curnode);
            } else if (curnode->child == 2) {
                _htm_node_prep2(curnode);
                _htm_node_make2(curnode);
            } else {
                _htm_node_make3(curnode);
            }
            ++curnode;
            ++curlevel;
        }
    }
    if (err != NULL) {
        *err = HTM_OK;
    }
    return ids;
}

}
