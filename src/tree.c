/** \file
    \brief      HTM tree index implementation.

    For API documentation, see tree.h.

    \authors    Serge Monkewitz
    \copyright  IPAC/Caltech
  */
#include "tinyhtm/tree.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "tinyhtm/varint.h"

#ifdef __cplusplus
extern "C" {
#endif


enum htm_errcode htm_tree_init(struct htm_tree *tree,
                               const char * const treefile,
                               const char * const datafile)
{
    struct stat sb;
    const unsigned char *s;
    uint64_t off, count;
    int i;
    const size_t pagesz =  (size_t) sysconf(_SC_PAGESIZE);
    enum htm_errcode err = HTM_OK;
    void *entries;

    /* set defaults */
    tree->leafthresh = 0;
    tree->count = 0;
    for (i = 0; i < 8; ++i) {
        tree->root[i] = NULL;
    }
    tree->entries = MAP_FAILED;
    tree->index = (const void *) MAP_FAILED;
    tree->indexsz = 0;
    tree->datasz = 0;
    tree->indexfd = -1;
    tree->datafd = -1;
    tree->element_types=NULL;
    tree->element_names=NULL;

    /* check inputs */
    if (tree == NULL || datafile == NULL) {
        return HTM_ENULLPTR;
    }
    if (stat(datafile, &sb) != 0) {
        return HTM_EIO;
    }

    /* Open with hdf5 commands just to get the size and offsets.  Then
       mmap with raw calls */
    {
      hid_t h5data;
      hid_t htm_dataset;
      hid_t htm_type;
      size_t i;

      h5data = H5Fopen(datafile, H5F_ACC_RDONLY,
                       H5P_DEFAULT);
      if (h5data < 0 ) {
        return HTM_EIO;
      }

      htm_dataset=H5Dopen(h5data,"htm", H5P_DEFAULT);
      if (htm_dataset < 0 ) {
        H5Fclose(h5data);
        return HTM_EIO;
      }
      tree->offset=H5Dget_offset(htm_dataset);
      tree->datasz = H5Dget_storage_size(htm_dataset);
      htm_type=H5Dget_type(htm_dataset);
      tree->entry_size=H5Tget_size(htm_type);

      tree->num_elements_per_entry=H5Tget_nmembers(htm_type);
      tree->element_types=malloc(sizeof(H5T_class_t)
                                 *tree->num_elements_per_entry);
      if(tree->element_types==NULL)
        {
          H5Dclose(htm_dataset);
          H5Fclose(h5data);
          return HTM_ENOMEM;
        }
      tree->element_names=malloc(sizeof(char*)*tree->num_elements_per_entry);
      if(tree->element_names==NULL)
        {
          H5Dclose(htm_dataset);
          H5Fclose(h5data);
          return HTM_ENOMEM;
        }
      for(i=0; i<tree->num_elements_per_entry; ++i)
        {
          tree->element_types[i]=H5Tget_member_class(htm_type,i);
          tree->element_names[i]=H5Tget_member_name(htm_type,i);
        }

      H5Dclose(htm_dataset);
      H5Fclose(h5data);
    }

     tree->datafd = open(datafile, O_RDONLY);
     if (tree->datafd == -1) {
         err = HTM_EIO;
         goto cleanup;
     }

    if (tree->datasz % tree->entry_size != 0 || tree->datasz == 0) {
        err=HTM_EINV;
        goto cleanup;
    }
    count = (uint64_t) tree->datasz / tree->entry_size;

    /* /\* memory map datafile *\/ */
    /* if (tree->datasz % pagesz != 0) { */
    /*     tree->datasz += pagesz - tree->datasz % pagesz; */
    /* } */

    entries = mmap(
        NULL, tree->datasz+tree->offset, PROT_READ, MAP_SHARED | MAP_NORESERVE,
        tree->datafd, 0);

    tree->entries=entries+tree->offset;
                                
    if (entries == MAP_FAILED) {
        err = HTM_EMMAN;
        goto cleanup;
    }

    if (madvise(entries, tree->datasz+tree->offset, MADV_RANDOM) != 0) {
        err = HTM_EMMAN;
        goto cleanup;
    }

    /* memory map treefile (if there is one) */
    if (treefile == NULL) {
        tree->count = count;
        return HTM_OK;
    }
    if (stat(treefile, &sb) != 0) {
        err = HTM_EIO;
        goto cleanup;
    }
    tree->indexsz = (size_t) sb.st_size;
    if (tree->indexsz % pagesz != 0) {
        tree->indexsz += pagesz - tree->indexsz % pagesz;
    }
    tree->indexfd = open(treefile, O_RDONLY);
    if (tree->indexfd == -1) {
        err = HTM_EIO;
        goto cleanup;
    }
    tree->index = (const void *) mmap(
        NULL, tree->indexsz, PROT_READ, MAP_SHARED | MAP_NORESERVE,
        tree->indexfd, 0);
    if ((void *) tree->index == MAP_FAILED) {
        err = HTM_EMMAN;
        goto cleanup;
    }
    if (madvise((void *) tree->index, tree->indexsz, MADV_RANDOM) != 0) {
        err = HTM_EMMAN;
        goto cleanup;
    }

    /* parse tree file header */
    s = (const unsigned char *) tree->index;
    tree->leafthresh = htm_varint_decode(s);
    s += 1 + htm_varint_nfollow(*s);
    tree->count = htm_varint_decode(s);
    s += 1 + htm_varint_nfollow(*s);
    if (tree->count != count) {
        /* tree index point count does not agree with data file */
        err = HTM_ETREE;
        goto cleanup;
    }
    for (i = 0; i < 8; ++i) {
        off = htm_varint_decode(s);
        s += 1 + htm_varint_nfollow(*s);
        if (off == 0) {
            tree->root[i] = NULL;
        } else {
            tree->root[i] = s + (off - 1);
        }
    }
    if (s - (const unsigned char *) tree->index >= sb.st_size) {
        /* header overflowed tree file size */
        err = HTM_ETREE;
        goto cleanup;
    }
    return HTM_OK;

cleanup:
    htm_tree_destroy(tree);
    return err;
}


void htm_tree_destroy(struct htm_tree *tree)
{
    size_t i;
    if (tree == NULL) {
        return;
    }
    /* unmap and close data file */
    if (tree->entries-tree->offset != MAP_FAILED) {
        munmap(tree->entries-tree->offset, tree->datasz);
        tree->entries = MAP_FAILED;
    }
    tree->datasz = 0;
    if (tree->datafd != -1) {
        close(tree->datafd);
        tree->datafd = -1;
    }
    /* unmap and close tree file */
    if (tree->index != MAP_FAILED) {
        munmap((void *)tree->index, tree->indexsz);
        tree->index = (const void *) MAP_FAILED;
    }
    tree->indexsz = 0;
    if (tree->indexfd != -1) {
        close(tree->indexfd);
        tree->indexfd = - 1;
    }
    /* Deallocate names and types */
    if(tree->element_names!=NULL)
      {
        for(i=0; i<tree->num_elements_per_entry; ++i)
          if(tree->element_names[i]!=NULL)
            free(tree->element_names[i]);
        free(tree->element_names);
      }
    if(tree->element_types!=NULL)
      free(tree->element_types);

    /* set remaining fields to default values */
    tree->leafthresh = 0;
    tree->count = 0;
    for (i = 0; i < 8; ++i) {
        tree->root[i] = NULL;
    }
}


enum htm_errcode htm_tree_lock(struct htm_tree *tree, size_t datathresh)
{
    if (tree == NULL) {
        return HTM_ENULLPTR;
    }
    if (tree->indexfd != -1) {
        if (mlock(tree->index, tree->indexsz) != 0) {
            return HTM_ENOMEM;
        }
    }
    if (tree->datasz <= datathresh) {
        if (mlock(tree->entries, tree->datasz) != 0) {
            return HTM_ENOMEM;
        }
    }
    return HTM_OK;
}


int64_t htm_tree_s2circle_scan(const struct htm_tree *tree,
                               const struct htm_v3 *center,
                               double radius,
                               enum htm_errcode *err)
{
    double dist2;
    int64_t count;
    uint64_t i;

    if (tree == NULL || center == NULL) {
        if (err != NULL) {
            *err = HTM_ENULLPTR;
        }
        return -1;
    }
    if (radius < 0.0) {
        return 0;
    } else if (radius >= 180.0) {
        return (int64_t) tree->count;
    }
    dist2 = sin(radius * 0.5 * HTM_RAD_PER_DEG);
    dist2 = 4.0 * dist2 * dist2;
    count = 0;
    for (i = 0, count = 0; i < tree->count; ++i) {
      if (htm_v3_dist2(center, (struct htm_v3*)(tree->entries+i*tree->entry_size)) <= dist2) {
            ++count;
        }
    }
    return count;
}


int64_t htm_tree_s2ellipse_scan(const struct htm_tree *tree,
                                const struct htm_s2ellipse *ellipse,
                                enum htm_errcode *err)
{
    int64_t count;
    uint64_t i;

    if (tree == NULL || ellipse == NULL) {
        if (err != NULL) {
            *err = HTM_ENULLPTR;
        }
        return -1;
    }
    count = 0;
    for (i = 0, count = 0; i < tree->count; ++i) {
      if (htm_s2ellipse_cv3(ellipse, (struct htm_v3*)(tree->entries+i*tree->entry_size)) != 0) {
            ++count;
        }
    }
    return count;
}


int64_t htm_tree_s2cpoly_scan(const struct htm_tree *tree,
                              const struct htm_s2cpoly *poly,
                              enum htm_errcode *err)
{
    int64_t count;
    uint64_t i;

    if (tree == NULL || poly == NULL) {
        if (err != NULL) {
            *err = HTM_ENULLPTR;
        }
        return -1;
    }
    count = 0;
    for (i = 0, count = 0; i < tree->count; ++i) {
      if (htm_s2cpoly_cv3(poly, (struct htm_v3*)(tree->entries+i*tree->entry_size)) != 0) {
            ++count;
        }
    }
    return count;
}


#ifdef __cplusplus
}
#endif

