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

#define USER_VADDR_BOTTOM ((void *) 0x0804000)

static void syscall_handler (struct intr_frame *);
const void *user_to_kernel_pointer(const void *vaddr);
static inline bool get_user(uint8_t *dst, const uint8_t *usrc);
static char *copy_in_string(const char *us);
void check_valid_buffer(void *buffer, unsigned size);
void check_valid_pointer(const void *vaddr);
void exit(int status);
int write(int fd, const void *buffer, unsigned size);
int wait(tid_t tid);
void halt(void);

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
                //printf("running exit\n");
                get_args(f, args, 1);
                f->eax = args[0];
                exit(args[0]);
                break;
            }
        case SYS_WRITE:
            {
                //printf("running write\n");
                get_args(f, args, 3);
                //printf("buffer before in\n");
                //printf("buffer should be %s and size of %d\n", (char *) args[1], args[2]);
                //check_valid_buffer((void *) args[1], (unsigned) args[2]);
                //args[1] = user_to_kernel_pointer((const void *) args[1]);
                //const void *test = user_to_kernel_pointer((const void *) args[1]);
                const void *test = (const void *) copy_in_string((const char *) args[1]);
                f->eax = write(args[0], test, (unsigned) args[2]);
                //printf("end of write call\n");
                break;
            }
        case SYS_WAIT:
            {
                //printf("running wait\n");
                get_args(f, args, 1);
                f->eax = wait(args[0]);
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

int
write(int fd, const void *buffer, unsigned size)
{
    //printf("inside write\n");
    if (fd == STDOUT_FILENO) {
        //printf("step 1\n");
        lock_acquire(&filesys_lock);
        //printf("step 2\nbuffer is %s\n", (const char*) buffer);
        putbuf(buffer, size);
        //printf("step 3\n");
        lock_release(&filesys_lock);
        //printf("step 4\n");
        return size;
    } else
        return -1;
}
    
int
wait(tid_t tid)
{
    return process_wait(tid);
}




static inline bool
get_user (uint8_t *dst, const uint8_t *usrc)
{
    int eax;
    asm ("movl $1f, %%eax; movb %2, %%al; movb %%al, %0; 1:"
         : "=m" (*dst), "=&a" (eax) : "m" (*usrc));
    return eax != 0;
}

static char *
copy_in_string (const char *us)
{
    char *ks;

    ks = palloc_get_page(0);
    if (ks == NULL)
        thread_exit();

    int i = 0;
    get_user((uint8_t *) ks, (const uint8_t *) us);
    while (*(us) != '\0') {
        //printf("the string is %s\n", us);
        get_user((uint8_t *) ks + ++i, (const uint8_t *) ++us); 
    }

    return ks;
}












void
get_args(struct intr_frame *f, int *arg, int n)
{
    int i;
    int *ptr;

    for (i = 0; i < n; i++) {
        ptr = (int *) f->esp + i + 1;
        arg[i] = *ptr;
    }
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
check_valid_pointer(const void *vaddr)
{
    if (!is_user_vaddr(vaddr) || vaddr < USER_VADDR_BOTTOM) {
        thread_exit();
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
    if (ptr == vaddr) {
        //printf("SAME SHIT\n");
    } else {
        //printf("NOT same %x and %x\n", (int) vaddr,(int)  ptr);
    }
    if (!ptr) {
        thread_exit();
    }
    //printf("done converting\n");
    return  ptr;
}
