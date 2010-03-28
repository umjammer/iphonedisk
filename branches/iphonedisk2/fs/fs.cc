// Author: Allen Porter <allen@thebends.org>
//
// A fuse filesystem that proxies the filesystem requests through a protocol
// buffer RPC service (FsService).

#include "fs.h"

#include <iostream>
#include <fuse.h>
#include <errno.h>
#include <strings.h>
#include <sys/stat.h>
#include "proto/fs.pb.h"
#include "proto/fs_service.pb.h"
#include "rpc/rpc.h"

namespace fs {

static const int kNameMax = 255;
static const int kFiles = 110000;
static const int kFilesFree = kFiles - 10000;

// Context information about filesystem.  This struct is available to every
// filesystem call.
struct FsContext {
  FsContext() : service(NULL) { }

  proto::FsService* service;
  std::string fs_id;
};

static google::protobuf::Closure* g_null_callback = NULL;

static void* fs_init(struct fuse_conn_info* conn) {
  struct FsContext* context =
    static_cast<struct FsContext*>(fuse_get_context()->private_data);
  fprintf(stderr, "fs_init: %s\n", context->fs_id.c_str());
  // Return value is passed in private_data of context for all other calls
  return context;
}

static void fs_destroy(void* data) {
  struct FsContext* context = static_cast<struct FsContext*>(data);
  fprintf(stderr, "fs_destroy: %s\n", context->fs_id.c_str());
}

static int fs_getattr(const char* path, struct stat* stbuf) {
  struct FsContext* context =
    static_cast<struct FsContext*>(fuse_get_context()->private_data);
  rpc::Rpc rpc;
  proto::GetAttrRequest request;
  proto::GetAttrResponse response;
  request.mutable_header()->set_fs_id(context->fs_id);
  request.set_path(path);
  context->service->GetAttr(&rpc, &request, &response, g_null_callback);
  if (rpc.Failed()) {
    return -ENOENT;
  }
  stbuf->st_size = response.stat().size();
  stbuf->st_blocks = response.stat().blocks();
  stbuf->st_mode = response.stat().mode();
  stbuf->st_nlink = response.stat().nlink();
  return 0; 
}

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi) {
  struct FsContext* context =
    static_cast<struct FsContext*>(fuse_get_context()->private_data);
  rpc::Rpc rpc;
  proto::ReadDirRequest request;
  proto::ReadDirResponse response;
  request.mutable_header()->set_fs_id(context->fs_id);
  request.set_path(path);
  context->service->ReadDir(&rpc, &request, &response, g_null_callback);
  if (rpc.Failed()) {
    return -ENOENT;
  }
  for (int i = 0; i < response.entry_size(); ++i) {
    filler(buf, response.entry(i).filename().c_str(), NULL, 0);
  }
  return 0; 
}

static int fs_unlink(const char* path) {
  struct FsContext* context =
    static_cast<struct FsContext*>(fuse_get_context()->private_data);
  rpc::Rpc rpc;
  proto::UnlinkRequest request;
  proto::UnlinkResponse response;
  request.mutable_header()->set_fs_id(context->fs_id);
  request.set_path(path);
  context->service->Unlink(&rpc, &request, &response, g_null_callback);
  return rpc.Failed() ? -ENOENT : 0;
}

static int fs_mkdir(const char* path, mode_t mode) {
  struct FsContext* context =
    static_cast<struct FsContext*>(fuse_get_context()->private_data);
  rpc::Rpc rpc;
  proto::MkDirRequest request;
  proto::MkDirResponse response;
  request.mutable_header()->set_fs_id(context->fs_id);
  request.set_path(path);
  request.set_mode(mode);
  context->service->MkDir(&rpc, &request, &response, g_null_callback);
  return rpc.Failed() ? -ENOENT : 0;
}

static int fs_rename(const char* from, const char* to) {
  struct FsContext* context =
    static_cast<struct FsContext*>(fuse_get_context()->private_data);
  rpc::Rpc rpc;
  proto::RenameRequest request;
  proto::RenameResponse response;
  request.mutable_header()->set_fs_id(context->fs_id);
  request.set_source_path(from);
  request.set_destination_path(to);
  context->service->Rename(&rpc, &request, &response, g_null_callback);
  return rpc.Failed() ? -ENOENT : 0;
}

static int fs_open(const char *path, struct fuse_file_info *fi) {
  struct FsContext* context =
    static_cast<struct FsContext*>(fuse_get_context()->private_data);
  rpc::Rpc rpc;
  proto::OpenRequest request;
  proto::OpenResponse response;
  request.mutable_header()->set_fs_id(context->fs_id);
  request.set_path(path);
  request.set_flags(fi->flags);
  context->service->Open(&rpc, &request, &response, g_null_callback);
  if (rpc.Failed()) {
    return -ENOENT;
  }
  fi->fh = response.filehandle();
  return 0;
}

static int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
  struct FsContext* context =
    static_cast<struct FsContext*>(fuse_get_context()->private_data);
  rpc::Rpc rpc;
  proto::CreateRequest request;
  proto::CreateResponse response;
  request.mutable_header()->set_fs_id(context->fs_id);
  request.set_path(path);
  request.set_flags(fi->flags);
  request.set_mode(mode);
  context->service->Create(&rpc, &request, &response, g_null_callback);
  if (rpc.Failed()) {
    return -ENOENT;
  }
  fi->fh = response.filehandle();
  return 0;
}

static int fs_release(const char *path, struct fuse_file_info *fi) {
  struct FsContext* context =
    static_cast<struct FsContext*>(fuse_get_context()->private_data);
  rpc::Rpc rpc;
  proto::ReleaseRequest request;
  proto::ReleaseResponse response;
  request.mutable_header()->set_fs_id(context->fs_id);
  request.set_filehandle(fi->fh);
  context->service->Release(&rpc, &request, &response, g_null_callback);
  return rpc.Failed() ? -ENOENT : 0;
}

static int fs_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
  struct FsContext* context =
    static_cast<struct FsContext*>(fuse_get_context()->private_data);
  rpc::Rpc rpc;
  proto::ReadRequest request;
  proto::ReadResponse response;
  request.mutable_header()->set_fs_id(context->fs_id);
  request.set_filehandle(fi->fh);
  request.set_size(size);
  request.set_offset(offset);
  context->service->Read(&rpc, &request, &response, g_null_callback);
  if (rpc.Failed()) {
    return -ENOENT;
  }
  memcpy(buf, response.buffer().data(), response.buffer().size());
  return response.buffer().size();
}

static int fs_write(const char *path, const char *buf, size_t size,
                    off_t offset, struct fuse_file_info *fi) {
  struct FsContext* context =
    static_cast<struct FsContext*>(fuse_get_context()->private_data);
  rpc::Rpc rpc;
  proto::WriteRequest request;
  proto::WriteResponse response;
  request.mutable_header()->set_fs_id(context->fs_id);
  request.set_filehandle(fi->fh);
  request.mutable_buffer()->assign(buf, size);
  request.set_offset(offset);
  context->service->Write(&rpc, &request, &response, g_null_callback);
  return rpc.Failed() ? -ENOENT : response.size();
}

static int fs_truncate(const char *path, off_t offset) {
  struct FsContext* context =
    static_cast<struct FsContext*>(fuse_get_context()->private_data);
  rpc::Rpc rpc;
  proto::TruncateRequest request;
  proto::TruncateResponse response;
  request.mutable_header()->set_fs_id(context->fs_id);
  request.set_path(path);
  request.set_offset(offset);
  context->service->Truncate(&rpc, &request, &response, g_null_callback);
  if (rpc.Failed()) {
    return -ENOENT;
  }
  return rpc.Failed() ? -ENOENT : 0;
}

static int fs_statfs(const char* path, struct statvfs* vfs) {
  struct FsContext* context =
    static_cast<struct FsContext*>(fuse_get_context()->private_data);
  rpc::Rpc rpc;
  proto::StatFsRequest request;
  proto::StatFsResponse response;
  request.mutable_header()->set_fs_id(context->fs_id);
  context->service->StatFs(&rpc, &request, &response, g_null_callback);
  if (rpc.Failed()) {
    return -ENOENT;
  }
  vfs->f_namemax = kNameMax;
  vfs->f_bsize = response.stat().bsize();
  vfs->f_frsize = response.stat().frsize();
  vfs->f_blocks = response.stat().blocks();
  vfs->f_bfree = response.stat().bfree();
  vfs->f_bavail = vfs->f_bfree;
  vfs->f_files = kFiles;
  vfs->f_ffree = kFilesFree;
  return 0;
}

static int fs_chown(const char* path, uid_t uid, gid_t) {
  return 0;
}

static int fs_chmod(const char* path, mode_t) {
  return 0;
}

static int fs_utimens(const char* path, const struct timespec tv[2]) {
  return 0;
}

void Initialize(struct fuse_operations* fuse_op) {
  assert(g_null_callback == NULL);
  g_null_callback = google::protobuf::NewPermanentCallback(
      &google::protobuf::DoNothing);
  bzero(fuse_op, sizeof(struct fuse_operations));
  fuse_op->init     = fs_init;
  fuse_op->destroy  = fs_destroy;
  fuse_op->getattr  = fs_getattr;
  fuse_op->readdir  = fs_readdir;
  fuse_op->open     = fs_open;
  fuse_op->create   = fs_create;
  fuse_op->release  = fs_release;
  fuse_op->read     = fs_read;
  fuse_op->write    = fs_write;
  fuse_op->truncate = fs_truncate;
  fuse_op->unlink   = fs_unlink;
  fuse_op->rename   = fs_rename;
  fuse_op->mkdir    = fs_mkdir;
  fuse_op->rmdir    = fs_unlink;
  fuse_op->statfs   = fs_statfs;
  fuse_op->chown    = fs_chown;
  fuse_op->chmod    = fs_chmod;
  fuse_op->utimens  = fs_utimens;
}

void InitFuseArgs(struct fuse_args* args, const std::string& volname) {
  *args = (struct fuse_args)FUSE_ARGS_INIT(0, NULL);
  fuse_opt_add_arg(args, "-d");
#ifdef DEBUG
  fuse_opt_add_arg(args, "-odebug");
#endif
  fuse_opt_add_arg(args, "-odefer_permissions");
  std::string volname_arg("-ovolname=");
  volname_arg.append(volname);
  fuse_opt_add_arg(args, volname_arg.c_str());
}

bool MountFilesystem(proto::FsService* service,
                     const std::string& fs_id,
                     const std::string& volname) {
  struct fuse_operations fuse_ops;
  Initialize(&fuse_ops);

  struct fuse_args args;
  InitFuseArgs(&args, volname);

  std::string mount_path = "/Volumes/";
  mount_path.append(volname);
  // Ignore errors
  rmdir(mount_path.c_str());
  mkdir(mount_path.c_str(), S_IFDIR|0755);

  struct fuse_chan* chan = fuse_mount(mount_path.c_str(), &args);
  if (chan == NULL) {
    std::cerr << fs_id << ": fuse_mount() failed" << std::endl;
    return false;
  }

  struct FsContext* context = new FsContext;
  context->service = service;
  context->fs_id = fs_id;

  struct fuse* f = fuse_new(chan, &args, &fuse_ops, sizeof(fuse_ops), context);
  if (f == NULL) {
    std::cerr << fs_id << ": fuse_new() failed" << std::endl;
    fuse_unmount(mount_path.c_str(), chan);
    return false;
  }
  int res = fuse_set_signal_handlers(fuse_get_session(f));
  if (res == -1) {
    fuse_unmount(mount_path.c_str(), chan);
    fuse_destroy(f);
    return false;
  }
  std::cout << "Fuse loop started." << std::endl;
  res = fuse_loop(f);
  std::cout << "Fuse loop exited." << std::endl;
  fuse_remove_signal_handlers(fuse_get_session(f));
  fuse_unmount(mount_path.c_str(), chan);
  fuse_destroy(f);
  delete context;
  return true;
}

}  // namespace fs
