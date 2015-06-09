#define FUSE_USE_VERSION 26

#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCKS_PER_FILE 32
#define BLOCK_SIZE 128

#define TYPE_NIL 0
#define TYPE_REG 1
#define TYPE_DIR 2
#define TYPE_LNK 4

// DATA STRUCTURES
struct Inode {
  char type;

  int size;
  int n_links;
  void* blocks[BLOCKS_PER_FILE];

  struct Inode *next;
  struct Inode *prev;
};

struct DirRecord {
  int inode_id; 
  char name[28];
};

// FUNCTION TEMPLATES:
static void init_fs();

static struct Inode *get_bad_inode(); 
static struct Inode *get_inode(int index);
static struct Inode *lookup(const char *path);
static void split_path(const char *path, char *parent, char *leaf);
static int get_inode_id(struct Inode *node);

static int myfs2_getattr(const char *path, struct stat *stbuf);
static int myfs2_link(const char* to, const char* from);
static int myfs2_mkdir(const char* path, mode_t mode);
static int myfs2_mknod(const char* path, mode_t mode, dev_t rdev);
static int myfs2_open(const char *path, struct fuse_file_info *fi);
static int myfs2_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int myfs2_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
static int myfs2_readlink(const char* path, char* buf, size_t size);
static int myfs2_rmdir(const char* path);
static int myfs2_symlink(const char* to, const char* from);
static int myfs2_truncate(const char *path, off_t offset);
static int myfs2_unlink(const char* path);
static int myfs2_utimens(const char* path, const struct timespec ts[2]);
static int myfs2_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

// VARIABLES:
static struct Inode *root_node = NULL;
static struct Inode *bad_node = NULL;

// IMPLEMENTATION:
static void init_fs() {
  bad_node = calloc(1, sizeof(struct Inode));

  root_node = calloc(1, sizeof(struct Inode));
  root_node->type = TYPE_DIR;
  root_node->n_links = 2;
  root_node->next = root_node->prev = NULL;
  
  root_node->blocks[0] = calloc(1, BLOCK_SIZE);
  struct DirRecord *list = (struct DirRecord*) root_node->blocks[0];
  strcpy(list[0].name,  ".");
  list[0].inode_id = 0; 
  strcpy(list[1].name, "..");
  list[1].inode_id = 0;
  root_node->size = 2;
}

static struct Inode *get_bad_inode() {
  struct Inode *curr = root_node;
  while (curr->type && curr->next) {
    curr = curr->next; 
  }
  if (!curr->type) {
    return curr;
  } else {
    return NULL;
  }
}

static struct Inode *get_inode(int index) {
  struct Inode *curr = root_node;
  while (index-- && curr->next) {
    curr = curr->next;
  }
  return curr;
}

static struct Inode *lookup(const char *path) {
  if (!strcmp(path, "/")) {
    return root_node;
  }

  char *pth = calloc(32, sizeof(char));
  strcpy(pth, path);

  char *delimiter = "/";
  struct Inode *current_inode = root_node;
  char *segment = strtok(pth, delimiter);

  struct DirRecord *records = NULL;
  char found_flag = 0;
  int watched = 0;
  while (segment) {
    found_flag = 0;
    if (current_inode->type != TYPE_DIR) {
      return bad_node;
    }
    watched = 0;
    int i; for (i = 0; i < BLOCKS_PER_FILE && watched < current_inode->size; ++i) {
      records = (struct DirRecord*) current_inode->blocks[i];
      found_flag = 0;
      int j; for (j = 0; j < 4 && watched < current_inode->size; ++j) {
        watched++;
        if (!strcmp(records[j].name, segment)) {
          current_inode = get_inode(records[j].inode_id);
          found_flag = 1;
          break;
        }
      }
      if (found_flag) {
        break;
      }
    }
    segment = strtok(NULL, delimiter);
  }
  if (!found_flag) {
    return bad_node;
  }
  return current_inode;
}

static void split_path(const char *path, char *parent, char *leaf) {
  char *pth = calloc(strlen(path) + 1, sizeof(char));
  strcpy(pth, path);

  char *last_slash = strrchr(pth, '/') + 1;
  strcpy(leaf, last_slash);
  last_slash[0] = '\0';
  strcpy(parent, pth);
  free(pth);
}

static int get_inode_id(struct Inode *node) {
  int index = 0;
  struct Inode *curr = root_node;
  while (node != curr && curr->next) {
    curr = curr->next;
    index++;
  }

  return index;
}

static int myfs2_getattr(const char *path, struct stat *stbuf) {
  int res = 0;
  
  memset(stbuf, 0, sizeof(struct stat));
  struct Inode *node = lookup(path);

  stbuf->st_nlink = node->n_links;
  switch (node->type) {
    case TYPE_NIL:
      res = -ENOENT;
      break;
    case TYPE_REG:
      stbuf->st_size = node->size;
      stbuf->st_mode = S_IFREG | 0776;
      break;
    case TYPE_DIR:
      stbuf->st_size = node->size;
      stbuf->st_mode = S_IFDIR | 0777;
      break;
    case TYPE_LNK:
      stbuf->st_size = node->size;
      stbuf->st_mode = S_IFLNK | 0777;
      break;
    default:
      res = -ENOENT;
      break;
  }
  return res;
}

static int myfs2_link(const char* to, const char* from) {
  struct Inode *node = lookup(to);
  node->n_links++;
  
  char* parent = calloc(1, BLOCK_SIZE);
  char* leaf = calloc(1, BLOCK_SIZE);

  split_path(from, parent, leaf);

  struct Inode *parent_dir = lookup(parent);
  
  int block_id = parent_dir->size / 4;
  int pos = parent_dir->size % 4;
  if (!pos) {
    parent_dir->blocks[block_id] = calloc(1, BLOCK_SIZE);
  }
  parent_dir->size = parent_dir->size + 1;

  void *ptr = parent_dir->blocks[block_id] + pos*sizeof(struct DirRecord);
  struct DirRecord *rec = (struct DirRecord*) ptr;

  rec->inode_id = get_inode_id(node);
  strcpy(rec->name, leaf);

  return 0;
}

static int myfs2_mkdir(const char* path, mode_t mode) {
  struct Inode *new_node = get_bad_inode();
  if (!new_node) {
    new_node = calloc(1, sizeof(struct Inode));
  }
  struct Inode *last_node = get_inode(-1);
  last_node->next = new_node;
  new_node->prev = last_node;

  new_node->n_links = 2;
  new_node->type = TYPE_DIR;
  new_node->next = NULL;

  int inode_id = get_inode_id(new_node);

  char* parent = calloc(1, BLOCK_SIZE);
  char* leaf = calloc(1, BLOCK_SIZE);

  split_path(path, parent, leaf);

  struct Inode *parent_dir = lookup(parent);
  
  int block_id = parent_dir->size / 4;
  int pos = parent_dir->size % 4;
  if (!pos) {
    parent_dir->blocks[block_id] = calloc(1, BLOCK_SIZE);
  }
  parent_dir->size = parent_dir->size + 1;
  parent_dir->n_links++;

  void *ptr = parent_dir->blocks[block_id] + pos*sizeof(struct DirRecord);
  struct DirRecord *rec = (struct DirRecord*) ptr;

  rec->inode_id = inode_id;
  strcpy(rec->name, leaf);

  new_node->blocks[0] = calloc(1, BLOCK_SIZE);
  struct DirRecord *list = (struct DirRecord*) new_node->blocks[0];
  strcpy(list[0].name,  ".");
  list[0].inode_id = inode_id; 
  strcpy(list[1].name, "..");
  list[1].inode_id = get_inode_id(parent_dir);
  new_node->size = 2;

  return 0; 
}

static int myfs2_mknod(const char* path, mode_t mode, dev_t rdev) { 
  struct Inode *new_node = get_bad_inode();
  if (!new_node) {
    new_node = calloc(1, sizeof(struct Inode));
  }
  new_node->n_links = 1;
  new_node->type = TYPE_REG;
  new_node->next = NULL;
  new_node->size = 0;

  struct Inode *last_node = get_inode(-1);
  last_node->next = new_node;
  new_node->prev = last_node;

  char* parent = calloc(1, BLOCK_SIZE);
  char* leaf = calloc(1, BLOCK_SIZE);

  split_path(path, parent, leaf);

  struct Inode *parent_dir = lookup(parent);
  
  int block_id = parent_dir->size / 4;
  int pos = parent_dir->size % 4;
  if (!pos) {
    parent_dir->blocks[block_id] = calloc(1, BLOCK_SIZE);
  }
  parent_dir->size = parent_dir->size + 1;

  void *ptr = parent_dir->blocks[block_id] + pos*sizeof(struct DirRecord);
  struct DirRecord *rec = (struct DirRecord*) ptr;

  rec->inode_id = get_inode_id(new_node);
  strcpy(rec->name, leaf);

  return 0;
}

static int myfs2_open(const char *path, struct fuse_file_info *fi) {
  struct Inode *node = lookup(path);
  if (node->type == TYPE_NIL) {
    return -ENOENT;
  }

  return 0;
}

static int myfs2_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  size_t len;
  (void) fi;
  
  struct Inode *node = lookup(path);
  if (node->type != TYPE_REG) {
    return -ENOENT;
  }

  len = node->size;

  int szz = len;

  char *data = calloc(1, size);
  char *dataptr = data;
  int idx = 0;
  int szz_to_read; 
  while (szz > 0) {
    szz_to_read = szz < BLOCK_SIZE ? szz : BLOCK_SIZE;
    memcpy(dataptr, node->blocks[idx++], szz_to_read);
    szz -= szz_to_read;
    dataptr += szz_to_read;
  }


  if (offset < len) {
    if (offset + size > len) {
      size = len - offset;
    }
    memcpy(buf, data + offset, size);
  } else {
    size = 0;
  }

  return size;
}

static int myfs2_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
  (void) offset;
  (void) fi;

  struct Inode *node = lookup(path);

  if (strncmp(path, "/", 1) != 0) {
    return -ENOENT;
  }

  if (node->type != TYPE_DIR) {
    return -ENOENT;
  }

  struct DirRecord *records = NULL;
  int watched = 0;
  int i; for (i = 0; i < BLOCKS_PER_FILE && watched < node->size; ++i) {
    records = (struct DirRecord*) node->blocks[i];
    int j; for (j = 0; j < 4 && watched < node->size; ++j) {
      watched++;
      filler(buf, records[j].name, NULL, 0);
    }
  }
 
  return 0;
}

static int myfs2_readlink(const char* path, char* buf, size_t size) {
  struct Inode *node = lookup(path);
  if (node->type != TYPE_LNK) {
    return -ENOENT;
  }
  
  char *realpath = node->blocks[0];
  memcpy(buf, realpath, size);
  return 0;
}

static int myfs2_rmdir(const char* path) {
  struct Inode *node = lookup(path);
  if (node->size > 2) {
    return -ENOTEMPTY;
  }

  int code = myfs2_unlink(path);

  char* parent = calloc(1, BLOCK_SIZE);
  char* leaf = calloc(1, BLOCK_SIZE);
  split_path(path, parent, leaf); 
  struct Inode *parent_node = lookup(parent);

  parent_node->n_links--;
 
  return 0 | code;
}

static int myfs2_symlink(const char* to, const char* from) {
  struct Inode *new_node = get_bad_inode();
  if (!new_node) {
    new_node = calloc(1, sizeof(struct Inode));
  }
  new_node->n_links = 1;
  new_node->type = TYPE_LNK;
  new_node->next = NULL;
  new_node->size = strlen(to);
  new_node->blocks[0] = calloc(1, strlen(to));
  strcpy(new_node->blocks[0], to);

  struct Inode *last_node = get_inode(-1);
  last_node->next = new_node;
  new_node->prev = last_node;

  char* parent = calloc(1, BLOCK_SIZE);
  char* leaf = calloc(1, BLOCK_SIZE);

  split_path(from, parent, leaf);

  struct Inode *parent_dir = lookup(parent);
  
  int block_id = parent_dir->size / 4;
  int pos = parent_dir->size % 4;
  if (!pos) {
    parent_dir->blocks[block_id] = calloc(1, BLOCK_SIZE);
  }
  parent_dir->size = parent_dir->size + 1;

  void *ptr = parent_dir->blocks[block_id] + pos*sizeof(struct DirRecord);
  struct DirRecord *rec = (struct DirRecord*) ptr;

  rec->inode_id = get_inode_id(new_node);
  strcpy(rec->name, leaf);

  return 0;  
}

static int myfs2_truncate(const char *path, off_t offset) {
  //placeholder
  return 0;
}

static int myfs2_unlink(const char* path) {
  struct Inode *to_unlink = lookup(path);
  int inode_id = get_inode_id(to_unlink);
  
  char* parent = calloc(1, BLOCK_SIZE);
  char* leaf = calloc(1, BLOCK_SIZE);
 
  split_path(path, parent, leaf);
  
  struct Inode *parent_node = lookup(parent);

  int block_id_1 = 0;
  int pos_1 = 0;  
  void *ptr = parent_node->blocks[block_id_1] + pos_1*sizeof(struct DirRecord);
  struct DirRecord *record = (struct DirRecord*) ptr;
  while(record->inode_id != inode_id) {
    pos_1++;
    if (pos_1 == 4) {
      block_id_1++;
      pos_1 = 0;
    }
    ptr = parent_node->blocks[block_id_1] + pos_1*sizeof(struct DirRecord);
    record = (struct DirRecord*) ptr;
  }

  int block_id_0 = (parent_node->size - 1) / 4;
  int pos_0 = (parent_node->size - 1) % 4;
  ptr = parent_node->blocks[block_id_0] + pos_0*sizeof(struct DirRecord);
  struct DirRecord *record_0 = (struct DirRecord*) ptr;
 
  strcpy(record->name, record_0->name);
  record->inode_id = record_0->inode_id; 

  parent_node->size = parent_node->size - 1;

  to_unlink->n_links--;
  if (to_unlink->n_links == 0) {
    to_unlink->type = TYPE_NIL;
    int i; for(i=0; to_unlink->size > 0 && i<BLOCKS_PER_FILE; ++i) {
      to_unlink->size -= to_unlink->size < BLOCK_SIZE ? to_unlink->size : BLOCK_SIZE;
      free(to_unlink->blocks[i]);
      to_unlink->blocks[i] = NULL;
    }
    to_unlink->size = 0;
  }

  return 0;
}

static int myfs2_utimens(const char* path, const struct timespec ts[2]) {
  //placeholder
  return 0;
}

static int myfs2_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
  size_t len;
  (void) fi;
  
  struct Inode *node = lookup(path);
  if (node->type == TYPE_NIL) {
    node = get_bad_inode();
    if (!node) {
      calloc(1, sizeof(struct Inode));
    }
    node->type = TYPE_REG;
    struct Inode *last_node = get_inode(-1);
    last_node->next = node;
    node->prev = last_node;
    node->size = 0;

    char* parent = calloc(1, BLOCK_SIZE);
    char* leaf = calloc(1, BLOCK_SIZE);

    struct Inode *parent_node = lookup(parent);
    int block_id = parent_node->size / 4;
    int pos = parent_node->size % 4;
    if (!pos) {
      parent_node->blocks[block_id] = calloc(1, BLOCK_SIZE);
    }
    parent_node->size = parent_node->size + 1;

    void *ptr = parent_node->blocks[block_id] + pos*sizeof(struct DirRecord);
    struct DirRecord *rec = (struct DirRecord*) ptr;

    rec->inode_id = get_inode_id(node);
    strcpy(rec->name, leaf);   
  }

  if (node->type != TYPE_REG) {
    return -ENOENT;
  }

  len = node->size;
  int szz = size;

  char *data = calloc(1, size);
  char *dataptr = data;
  memcpy(data, buf, size);
  if (offset <= len) {
    int block_id = offset / BLOCK_SIZE, block_offset = offset % BLOCK_SIZE;
    while(szz > 0 && block_id < BLOCKS_PER_FILE) { 
      if (!node->blocks[block_id]) {
        node->blocks[block_id] = calloc(1, BLOCK_SIZE);
      }
      memcpy(node->blocks[block_id] + block_offset, dataptr, szz < (BLOCK_SIZE - block_offset) ? szz : (BLOCK_SIZE - block_offset));
      dataptr += szz < (BLOCK_SIZE - block_offset) ? szz : (BLOCK_SIZE - block_offset);
      szz -= szz < (BLOCK_SIZE - block_offset) ? szz : (BLOCK_SIZE - block_offset);
      if (szz > 0) {
        block_offset = 0;
        block_id++;
      }
    }
    
    size -= szz;
    node->size = offset + size;
    free(data);
  } else {
    // this should never happen
    size = 0;
  }

  return size;
}

// BIND TO DUSE:
static struct fuse_operations myfs2_oper = {
  .getattr = myfs2_getattr,
  .link = myfs2_link,
  .mkdir = myfs2_mkdir,
  .mknod = myfs2_mknod,
  .open = myfs2_open,
  .read = myfs2_read,
  .readdir = myfs2_readdir,
  .readlink = myfs2_readlink,
  .rmdir = myfs2_rmdir,
  .symlink = myfs2_symlink,
  .truncate = myfs2_truncate,
  .unlink = myfs2_unlink,
  .utimens = myfs2_utimens,
  .write = myfs2_write,
};

// RUN:
int main(int argc, char *argv[]) { 
  init_fs();
  return fuse_main(argc, argv, &myfs2_oper, NULL);
}

