#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"

#define USER_VADDR_BOTTOM ((void *) 0x0804000)

static void syscall_handler (struct intr_frame *);
const void *user_to_kernel_pointer(const void *vaddr);
static inline bool get_user(uint8_t *dst, const uint8_t *usrc);
static char *copy_in_string(const char *us);
void check_valid_buffer(void *buffer, unsigned size);
void check_valid_pointer(const void *vaddr);
void exit(int status);
tid_t exec(const char *cmd_line);
int write(int fd, const void *buffer, unsigned size);
int wait(tid_t tid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
void halt(void);
void seek(int fd, unsigned position);
unsigned tell(int fd);
int open(const char *file);
void close(int fd);

struct lock filesys_lock;
void get_args(struct intr_frame *f, int *arg, int n);

void
syscall_init (void) 
{
    lock_init(&filesys_lock);
    intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void// 
syscall_handler (struct intr_frame *f) 
{
    int args[3];
    // The f-esp is void *, stack memory location, so dereference at location to get value
    switch (* (int *) f->esp)
    {
        case SYS_HALT:
            {
                halt();
                break;
            }
        case SYS_EXIT:
            {
                //printf("exiting\n");
                get_args(f, args, 1);
                f->eax = args[0];
                exit(args[0]);
                break;
            }
        case SYS_EXEC:
            {
                f->eax = 10;
                get_args(f, args, 1);
                args[0] = user_to_kernel_pointer((const void *) args[0]);
                f->eax = exec((const char *) args[0]);
                break;
            }
        case SYS_WRITE:
            {
                //printf("writing file\n");
                get_args(f, args, 3);
                check_valid_buffer((void *) args[1], (unsigned) args[2]);
                const void *test = user_to_kernel_pointer((const void *) args[1]);
                f->eax = write(args[0], test, (unsigned) args[2]);
                break;
            }
        case SYS_WAIT:
            {
                get_args(f, args, 1);
                f->eax = wait(args[0]);
                break;
            }
        case SYS_CREATE:
            {
                //printf("creating file\n");
                get_args(f, args, 2);
                args[0] = user_to_kernel_pointer((const void *) args[0]);
                f->eax = create((const char *) args[0], (unsigned) args[1]);
                break;
            }
        case SYS_REMOVE:
            {
                //printf("removing file\n");
                get_args(f, args, 1);
                args[0] = user_to_kernel_pointer((const void *) args[0]);
                f->eax = remove((const char *) args[0]);
            }
        case SYS_OPEN:
            {
                //printf("opening file\n");
                get_args(f, args, 1);
                args[0] = user_to_kernel_pointer((const void *) args[0]);
                f->eax = open((const char *) args[0]);
                break;
            }
        case SYS_FILESIZE:
            {
                //printf("checking filesize\n");
                get_args(f, args, 1);
                f->eax = filesize(args[0]);
            }
        case SYS_READ:
            {
                //printf("reading file\n");
                get_args(f, args, 3);
                check_valid_buffer((void *) args[1], (unsigned) args[2]);
                args[1] = user_to_kernel_pointer((const void *) args[1]);
                f->eax = read(args[0], (void *) args[1], (unsigned) args[2]);
                break;
            }
        case SYS_SEEK:
            {
                //printf("seeking file\n");
                get_args(f, args, 2);
                seek(args[0], (unsigned) args[1]);
                break;
            }
        case SYS_TELL:
            {
                //printf("telling file\n");
                get_args(f, args, 1);
                seek(args[0], (unsigned) args[1]);
                break;
            }
        case SYS_CLOSE:
            {
                //printf("closing file\n");
                get_args(f, args, 1);
                close(args[0]);
                break;
            }


    }
}

void
halt(void)
{
    shutdown_power_off();
}

void
exit(int status)
{
    printf("%s: exit(%d)\n", thread_current()->name, status);
    thread_exit();
}

tid_t
exec(const char *cmd_line)
{
    lock_acquire(&filesys_lock);
    //printf("locked in exec\n");
    tid_t pid = process_execute(cmd_line);
    lock_release(&filesys_lock);
    //printf("released in exec\n");
    return pid;
}

int
wait(tid_t tid)
{
    return process_wait(tid);
}

int
write(int fd, const void *buffer, unsigned size)
{
    if (fd == STDOUT_FILENO) {
        lock_acquire(&filesys_lock);
        putbuf(buffer, size);
        lock_release(&filesys_lock);
        return size;
    } else {
        struct thread *t = thread_current();
        uint8_t *buf = buffer;  // NEEDED???

        struct file_handle *fh = thread_get_fh(&t->files, fd);

        if (fh == NULL) {
            exit(-1);
        }

        lock_acquire(&filesys_lock);
        off_t wrote = file_write(fh->file, buf, size);
        lock_release(&filesys_lock);

        return wrote;
    }
}
        
void
seek(int fd, unsigned position)
{
    struct thread *t = thread_current();
    struct file_handle *fh = thread_get_fh(&t->files, fd);

    if (fh == NULL) {
        exit(-1);
    }

    lock_acquire(&filesys_lock);
    file_seek(fh->file, position);
    lock_release(&filesys_lock);
}

bool
create(const char *file, unsigned initial_size)
{
    lock_acquire(&filesys_lock);
    bool i = filesys_create(file, initial_size);
    lock_release(&filesys_lock);
    return i;
}

bool
remove(const char *file)
{
    lock_acquire(&filesys_lock);
    bool i = filesys_remove(file);
    lock_release(&filesys_lock);
    return i;
}

int
filesize(int fd)
{
    struct thread *t = thread_current();
    struct file_handle *fh = thread_get_fh(&t->files, fd);

    if (fh == NULL) {
        exit(-1);
    }

    return file_length(fh->file);
}

int
read(int fd, void *buffer, unsigned size)
{
    struct thread *t = thread_current();
    if (fd == 0) {
        unsigned i;
        uint8_t *buf = buffer;

        lock_acquire(&filesys_lock);
        for (i = 0; i < size; i++) {
            buf[i] = input_getc();
        }
        lock_release(&filesys_lock);
        return size;
    } else {
        uint8_t *buf = buffer;

        struct file_handle *fh = thread_get_fh(&t->files, fd);

        if (fh == NULL) {
            exit(-1);
        }

        lock_acquire(&filesys_lock);
        off_t read = file_read(fh->file, buf, size);
        lock_release(&filesys_lock);
 
        return read;
    }
}

unsigned
tell(int fd)
{
    struct thread *t = thread_current();
    struct file_handle *fh = thread_get_fh(&t->files, fd);
    
    if (fh == NULL) {
        exit(-1);
    }

    lock_acquire(&filesys_lock);
    off_t position = file_tell(fh->file);
    lock_release(&filesys_lock);
    return position;
}

void
get_args(struct intr_frame *f, int *arg, int n)
{
    int i;
    int *ptr;

    for (i = 0; i < n; i++) {
        //printf("getting arg %d\n %x\n", i, ((int *)f->esp + i + 1));
        check_valid_pointer((int *) f->esp + i + 1);
        ptr = (int *) f->esp + i + 1;
        arg[i] = *ptr;
    }
}

int
open(const char *file)
{
    lock_acquire(&filesys_lock);
    struct file *f = filesys_open(file);
    lock_release(&filesys_lock);

    int fd;
    if (f == NULL) {
        fd = -1;
    } else {
        fd = thread_add_fd(f);
    }

    return fd;
}

void 
check_valid_buffer(void *buffer, unsigned size)
{
    //printf("Checking valid buffer\n");
    unsigned i;
    char *local_buffer = (char *) buffer;
    for (i = 0; i < size; i++) {
        check_valid_pointer((const void *) local_buffer);
        local_buffer++;
    }
    //printf("Buffer was valid\n");
}

void
close(int fd)
{
    struct thread *t = thread_current();
    struct file_handle *fh = thread_get_fh(&t->files, fd);

    if (fh == NULL) {
        exit(-1);
    }

    lock_acquire(&filesys_lock);
    file_close(fh->file);
    thread_remove_file(fh);
    lock_release(&filesys_lock);
}

void
check_valid_pointer(const void *vaddr)
{
    if (!is_user_vaddr(vaddr) || !pagedir_get_page(thread_current()->pagedir, vaddr) ) {
        //printf("BAD!\n\n");
        exit(-1);
    }
}

const void *
user_to_kernel_pointer(const void *vaddr)
{
    //printf("converting pointer\n");
    //printf("checking if pointer is valid\n");
    check_valid_pointer(vaddr);
    //printf("it was\n");
    void *ptr = pagedir_get_page(thread_current()->pagedir, vaddr);

    if (!ptr) {
        exit(-1);
    }
    //printf("done converting\n");
    return  ptr;
}
