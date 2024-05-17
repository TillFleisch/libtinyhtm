/** \file
    \brief      HTM tree index implementation.

    For API documentation, see tree.h.
e
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

extern "C" {

enum htm_errcode htm_tree_init (struct htm_tree *tree,
                                const char *const datafile)
{
  struct stat sb;
  const unsigned char *s;
  uint64_t off, count;
  int i;
  enum htm_errcode err = HTM_OK;
  void *data_mmap;
  size_t mmap_size, index_offset;

  /* set defaults */
  tree->leafthresh = 0;
  tree->count = 0;
  for (i = 0; i < 8; ++i)
    {
      tree->root[i] = NULL;
    }
  tree->entries = MAP_FAILED;
  tree->index = (const void *)MAP_FAILED;
  tree->indexsz = 0;
  tree->datasz = 0;
  tree->datafd = -1;

  index_offset = 0;

  /* check inputs */
  if (tree == NULL || datafile == NULL)
    {
      return HTM_ENULLPTR;
    }
  if (stat (datafile, &sb) != 0)
    {
      return HTM_EIO;
    }

  tree->datafd = open (datafile, O_RDONLY);
  if (tree->datafd == -1)
    {
      err = HTM_EIO;
      goto cleanup;
    }

  if (tree->datasz % tree->entry_size != 0 || tree->datasz == 0)
    {
      err = HTM_EINV;
      goto cleanup;
    }
  count = (uint64_t)tree->datasz / tree->entry_size;

  /* /\* memory map datafile *\/ */
  /* if (tree->datasz % pagesz != 0) { */
  /*     tree->datasz += pagesz - tree->datasz % pagesz; */
  /* } */

  mmap_size = (tree->datasz + tree->offset > tree->indexsz + index_offset)
                  ? (tree->datasz + tree->offset)
                  : (tree->indexsz + index_offset);

  data_mmap = mmap (NULL, mmap_size, PROT_READ, MAP_SHARED | MAP_NORESERVE,
                    tree->datafd, 0);

  tree->entries = static_cast<char *>(data_mmap) + tree->offset;

  if (data_mmap == MAP_FAILED)
    {
      err = HTM_EMMAN;
      goto cleanup;
    }

  if (madvise (data_mmap, tree->datasz + tree->offset, MADV_RANDOM) != 0)
    {
      err = HTM_EMMAN;
      goto cleanup;
    }

  /* Make sure index exists */
  if (index_offset == 0)
    {
      tree->count = count;
      return HTM_OK;
    }

  tree->index = static_cast<char *>(data_mmap) + index_offset;

  /* parse tree file header */
  s = (const unsigned char *)tree->index;
  tree->leafthresh = htm_varint_decode (s);
  s += 1 + htm_varint_nfollow (*s);
  tree->count = htm_varint_decode (s);
  s += 1 + htm_varint_nfollow (*s);
  if (tree->count != count)
    {
      /* tree index point count does not agree with data file */
      err = HTM_ETREE;
      goto cleanup;
    }
  for (i = 0; i < 8; ++i)
    {
      off = htm_varint_decode (s);
      s += 1 + htm_varint_nfollow (*s);
      if (off == 0)
        {
          tree->root[i] = NULL;
        }
      else
        {
          tree->root[i] = s + (off - 1);
        }
    }
  if (s - (const unsigned char *)tree->index >= sb.st_size)
    {
      /* header overflowed tree file size */
      err = HTM_ETREE;
      goto cleanup;
    }
  return HTM_OK;

cleanup:
  htm_tree_destroy (tree);
  return err;
}

void htm_tree_destroy (struct htm_tree *tree)
{
  size_t i;
  if (tree == NULL)
    {
      return;
    }
  /* unmap and close data file */
  if (static_cast<char *>(tree->entries) - tree->offset != MAP_FAILED)
    {
      munmap (static_cast<char *>(tree->entries) - tree->offset, tree->datasz);
      tree->entries = MAP_FAILED;
    }
  tree->datasz = 0;
  if (tree->datafd != -1)
    {
      close (tree->datafd);
      tree->datafd = -1;
    }
  tree->index = (const void *)MAP_FAILED;
  tree->indexsz = 0;
  // /* Deallocate names and types */
  // if(tree->element_names!=NULL)
  //   {
  //     for(i=0; i<tree->num_elements_per_entry; ++i)
  //       if(tree->element_names[i]!=NULL)
  //         free(tree->element_names[i]);
  //     free(tree->element_names);
  //   }
  // if(tree->element_types!=NULL)
  //   free(tree->element_types);

  /* set remaining fields to default values */
  tree->leafthresh = 0;
  tree->count = 0;
  for (i = 0; i < 8; ++i)
    {
      tree->root[i] = NULL;
    }
}

enum htm_errcode htm_tree_lock (struct htm_tree *tree, size_t datathresh)
{
  if (tree == NULL)
    {
      return HTM_ENULLPTR;
    }
  if (tree->index != MAP_FAILED)
    {
      if (mlock (tree->index, tree->indexsz) != 0)
        {
          return HTM_ENOMEM;
        }
    }
  if (tree->datasz <= datathresh)
    {
      if (mlock (tree->entries, tree->datasz) != 0)
        {
          return HTM_ENOMEM;
        }
    }
  return HTM_OK;
}
}
