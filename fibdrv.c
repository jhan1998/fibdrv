#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/uaccess.h>

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("Fibonacci engine driver");
MODULE_VERSION("0.1");

#define DEV_FIBONACCI_NAME "fibonacci"

/* MAX_LENGTH is set to 92 because
 * ssize_t can't fit the number > 92
 */
#define MAX_LENGTH 500

#define MAX_STR_LEN_BITS (54)
#define MAX_STR_LEN ((1UL << MAX_STR_LEN_BITS) - 1)

#define LARGE_STRING_LEN 256

typedef union {
    /* allow strings up to 15 bytes to stay on the stack
     * use the last byte as a null terminator and to store flags
     * much like fbstring:
     * https://github.com/facebook/folly/blob/master/folly/docs/FBString.md
     */
    char data[16];

    struct {
        uint8_t filler[15],
            /* how many free bytes in this stack allocated string
             * same idea as fbstring
             */
            space_left : 4,
            /* if it is on heap, set to 1 */
            is_ptr : 1, is_large_string : 1, flag2 : 1, flag3 : 1;
    };

    /* heap allocated */
    struct {
        char *ptr;
        /* supports strings up to 2^MAX_STR_LEN_BITS - 1 bytes */
        size_t size : MAX_STR_LEN_BITS,
                      /* capacity is always a power of 2 (unsigned)-1 */
                      capacity : 6;
        /* the last 4 bits are important flags */
    };
} xs;

static inline bool xs_is_ptr(const xs *x)
{
    return x->is_ptr;
}

static inline bool xs_is_large_string(const xs *x)
{
    return x->is_large_string;
}

static inline size_t xs_size(const xs *x)
{
    return xs_is_ptr(x) ? x->size : 15 - x->space_left;
}

static inline char *xs_data(const xs *x)
{
    if (!xs_is_ptr(x))
        return (char *) x->data;

    if (xs_is_large_string(x))
        return (char *) (x->ptr + 4);
    return (char *) x->ptr;
}

// static inline size_t xs_capacity(const xs *x)
// {
//     return xs_is_ptr(x) ? ((size_t) 1 << x->capacity) - 1 : 15;
// }

static inline void xs_set_refcnt(const xs *x, int val)
{
    *((int *) ((size_t) x->ptr)) = val;
}

// static inline void xs_inc_refcnt(const xs *x)
// {
//     if (xs_is_large_string(x))
//         ++(*(int *) ((size_t) x->ptr));
// }

// static inline int xs_dec_refcnt(const xs *x)
// {
//     if (!xs_is_large_string(x))
//         return 0;
//     return --(*(int *) ((size_t) x->ptr));
// }

// static inline int xs_get_refcnt(const xs *x)
// {
//     if (!xs_is_large_string(x))
//         return 0;
//     return *(int *) ((size_t) x->ptr);
// }

#define xs_literal_empty() \
    (xs) { .space_left = 15 }

/* lowerbound (floor log2) */
static inline int log2(uint32_t n)
{
    return 32 - __builtin_clz(n) - 1;
}

static void xs_allocate_data(xs *x, size_t len, bool reallocate)
{
    /* Medium string */
    if (len < LARGE_STRING_LEN) {
        x->ptr = reallocate
                     ? krealloc(x->ptr, (size_t) 1 << x->capacity, GFP_KERNEL)
                     : kmalloc((size_t) 1 << x->capacity, GFP_KERNEL);
        return;
    }

    /* Large string */
    x->is_large_string = 1;

    /* The extra 4 bytes are used to store the reference count */
    x->ptr = reallocate
                 ? krealloc(x->ptr, (size_t)(1 << x->capacity) + 4, GFP_KERNEL)
                 : kmalloc((size_t)(1 << x->capacity) + 4, GFP_KERNEL);

    xs_set_refcnt(x, 1);
}

xs *xs_new(xs *x, const void *p)
{
    *x = xs_literal_empty();
    size_t len = strlen(p) + 1;
    if (len > 16) {
        x->capacity = log2(len) + 1;
        x->size = len - 1;
        x->is_ptr = true;
        xs_allocate_data(x, x->size, 0);
        memcpy(xs_data(x), p, len);
    } else {
        memcpy(x->data, p, len);
        x->space_left = 15 - (len - 1);
    }
    return x;
}

/* Memory leaks happen if the string is too long but it is still useful for
 * short strings.
 */
#define xs_tmp(x)                                                   \
    ((void) ((struct {                                              \
         _Static_assert(sizeof(x) <= MAX_STR_LEN, "it is too big"); \
         int dummy;                                                 \
     }){1}),                                                        \
     xs_new(&xs_literal_empty(), x))

// /* grow up to specified size */
// xs *xs_grow(xs *x, size_t len)
// {
//     char buf[16];

//     // if (len <= xs_capacity(x))
//     //     return x;

//     /* Backup first */
//     if (!xs_is_ptr(x))
//         memcpy(buf, x->data, 16);

//     x->is_ptr = true;
//     x->capacity = log2(len) + 1;

//     if (xs_is_ptr(x)) {
//         xs_allocate_data(x, len, 1);
//     } else {
//         xs_allocate_data(x, len, 0);
//         memcpy(xs_data(x), buf, 16);
//     }
//     return x;
// }

// static inline xs *xs_newempty(xs *x)
// {
//     *x = xs_literal_empty();
//     return x;
// }

// static inline xs *xs_free(xs *x)
// {
//     if (xs_is_ptr(x) && xs_dec_refcnt(x) <= 0)
//         kfree(x->ptr);
//     return xs_newempty(x);
// }

static inline void __swap(void *a, void *b)
{
    xs tmp = *(xs *) a;
    *(xs *) a = *(xs *) b;
    *(xs *) b = tmp;
}

static inline void reverse_str(char *str, size_t size)
{
    int head = 0, tail = size - 1;
    while (head < tail) {
        char tmp = str[head];
        str[head] = str[tail];
        str[tail] = tmp;
        head++;
        tail--;
    }
    return;
}

static void string_number_add(xs *a, xs *b, xs *out)
{
    char *data_a, *data_b;
    int i, carry = 0;
    int sum;
    size_t size_a, size_b;

    /*
     * Make sure the string length of 'a' is always greater than
     * the one of 'b'.
     */
    if (xs_size(a) < xs_size(b))
        __swap((void *) &a, (void *) &b);

    data_a = xs_data(a);
    data_b = xs_data(b);

    size_a = xs_size(a);
    size_b = xs_size(b);

    reverse_str(data_a, size_a);
    reverse_str(data_b, size_b);

    char buf[size_a + 2];

    for (i = 0; i < size_b; i++) {
        sum = (data_a[i] - '0') + (data_b[i] - '0') + carry;
        buf[i] = '0' + sum % 10;
        carry = sum / 10;
    }

    for (i = size_b; i < size_a; i++) {
        sum = (data_a[i] - '0') + carry;
        buf[i] = '0' + sum % 10;
        carry = sum / 10;
    }

    if (carry)
        buf[i++] = '0' + carry;

    buf[i] = 0;

    reverse_str(buf, i);

    /* Restore the original string */
    reverse_str(data_a, size_a);
    reverse_str(data_b, size_b);

    if (out) {
        BUG_ON(sizeof(buf) >= MAX_STR_LEN);
        *out = *xs_new(&xs_literal_empty(), buf);
    }
}


static dev_t fib_dev = 0;
static struct cdev *fib_cdev;
static struct class *fib_class;
static DEFINE_MUTEX(fib_mutex);

static xs fib_sequence_str(long long k)
{
    /* FIXME: use clz/ctz and fast algorithms to speed up */
    xs f[k + 2];

    f[0] = *xs_new(&xs_literal_empty(), "0");
    f[1] = *xs_new(&xs_literal_empty(), "1");
    for (int i = 2; i <= k; i++) {
        string_number_add(&f[i - 1], &f[i - 2], &f[i]);
    }
    return f[k];
}

// static long long fib_sequence(long long k)
// {
//     /* FIXME: use clz/ctz and fast algorithms to speed up */
//     long long f[k + 2];

//     f[0] = 0;
//     f[1] = 1;

//     for (int i = 2; i <= k; i++) {
//         f[i] = f[i - 1] + f[i - 2];
//     }

//     return f[k];
// }

static int fib_open(struct inode *inode, struct file *file)
{
    if (!mutex_trylock(&fib_mutex)) {
        printk(KERN_ALERT "fibdrv is in use");
        return -EBUSY;
    }
    return 0;
}

static int fib_release(struct inode *inode, struct file *file)
{
    mutex_unlock(&fib_mutex);
    return 0;
}

/* calculate the fibonacci number at given offset */
static ssize_t fib_read(struct file *file,
                        char *buf,
                        size_t size,
                        loff_t *offset)
{
    long long int k = *offset;
    xs fib = fib_sequence_str(k);
    printk("%lld %s\n", *offset, xs_data(&fib));
    copy_to_user(buf, xs_data(&fib), strlen(xs_data(&fib)) + 1);
    return 0;
}

/* write operation is skipped */
static ssize_t fib_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    return 1;
}

static loff_t fib_device_lseek(struct file *file, loff_t offset, int orig)
{
    loff_t new_pos = 0;
    switch (orig) {
    case 0: /* SEEK_SET: */
        new_pos = offset;
        break;
    case 1: /* SEEK_CUR: */
        new_pos = file->f_pos + offset;
        break;
    case 2: /* SEEK_END: */
        new_pos = MAX_LENGTH - offset;
        break;
    }

    if (new_pos > MAX_LENGTH)
        new_pos = MAX_LENGTH;  // max case
    if (new_pos < 0)
        new_pos = 0;        // min case
    file->f_pos = new_pos;  // This is what we'll use now
    return new_pos;
}

const struct file_operations fib_fops = {
    .owner = THIS_MODULE,
    .read = fib_read,
    .write = fib_write,
    .open = fib_open,
    .release = fib_release,
    .llseek = fib_device_lseek,
};

static int __init init_fib_dev(void)
{
    int rc = 0;

    mutex_init(&fib_mutex);

    // Let's register the device
    // This will dynamically allocate the major number
    rc = alloc_chrdev_region(&fib_dev, 0, 1, DEV_FIBONACCI_NAME);

    if (rc < 0) {
        printk(KERN_ALERT
               "Failed to register the fibonacci char device. rc = %i",
               rc);
        return rc;
    }

    fib_cdev = cdev_alloc();
    if (fib_cdev == NULL) {
        printk(KERN_ALERT "Failed to alloc cdev");
        rc = -1;
        goto failed_cdev;
    }
    fib_cdev->ops = &fib_fops;
    rc = cdev_add(fib_cdev, fib_dev, 1);

    if (rc < 0) {
        printk(KERN_ALERT "Failed to add cdev");
        rc = -2;
        goto failed_cdev;
    }

    fib_class = class_create(THIS_MODULE, DEV_FIBONACCI_NAME);

    if (!fib_class) {
        printk(KERN_ALERT "Failed to create device class");
        rc = -3;
        goto failed_class_create;
    }

    if (!device_create(fib_class, NULL, fib_dev, NULL, DEV_FIBONACCI_NAME)) {
        printk(KERN_ALERT "Failed to create device");
        rc = -4;
        goto failed_device_create;
    }
    return rc;
failed_device_create:
    class_destroy(fib_class);
failed_class_create:
    cdev_del(fib_cdev);
failed_cdev:
    unregister_chrdev_region(fib_dev, 1);
    return rc;
}

static void __exit exit_fib_dev(void)
{
    mutex_destroy(&fib_mutex);
    device_destroy(fib_class, fib_dev);
    class_destroy(fib_class);
    cdev_del(fib_cdev);
    unregister_chrdev_region(fib_dev, 1);
}

module_init(init_fib_dev);
module_exit(exit_fib_dev);
