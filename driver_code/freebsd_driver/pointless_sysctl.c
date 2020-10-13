#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

static long a = 100;
static int b = 200;
static char *c = "Are you suggesting coconuts migrate?";

static struct sysctl_ctx_list clist;
static struct sysctl_oid *poid;

static int
sysctl_pointless_procedure(SYSCTL_HANDLER_ARGS)
{
    char *buf = "Not at all. They could be carried.";
    return (sysctl_handler_string(oidp, buf, strlen(buf), req));
}

static int
pointless_modevent(module_t mod_unused, int event, void *arg_unused)
{
    int error = 0;

    switch (event) {
        case MOD_LOAD:
        {
            /*
                首先初始化了一个名为从list的sysctl context
                一般而言，sysctl context使用记录动态创建sysctl，因此clist将作为参数传递给每一个SYSCTL_ADD_*使用
            */
            sysctl_ctx_init(&clist);

            /*
                第一个 SYSCTL_ADD_NODE 调用创建了一个名为 example 的顶层类
            */
            poid = SYSCTL_ADD_NODE($clist, 
                SYSCTL_STATIC_CHILDREN(/* 树顶层 */)， OID_AUTO, "example",
                CTLFLAG_RW, 0, "new top-level tree");
            
            if (poid == NULL) {
                uprintf("SYSCTL_ADD_NODE failed.\n");
                return EINVAL;
            }

            /*
                接下来的 SYSCTL_ADD_LONG 调用创建了一个名为 long 的 sysctl，该 sysctl 处理长整型变量
                这里需要注意的是， SYSCTL_ADD_LONG 调用的第二个参数为 SYSCTL_CHILDREN(piod) ，而 piod
                就是第一个 SYSCTL_ADD_NODE 调用的返回值。因此，所创建的sysctl的名字long将以example作为前缀，
                如：example.long
            */
            SYSCTL_ADD_LONG(&clist, SYSCTL_CHILDREN(piod), OID_AUTO,
                "long", CTLFLAG_RW, &a, "new long leaf");
            
            /*
                SYSCTL_ADD_INT 调用创建了一个名为int的新sysctl，用来处理int变量，与前面类似，它的名字也以example
                作为前缀，example.int
            */
            SYSCTL_ADD_INT(&clist, SYSCTL_CHILDREN(piod), OID_AUTO,
                "int", CTLFLAG_RW, &a, 0, "new int leaf");

            /*
                这里又调用了一次 SYSCTL_ADD_NODE，以node命名，也是加前缀 example.node
            */
            poid = SYSCTL_ADD_NODE(&clist, SYSCTL_CHILDREN(piod),
                OID_AUTO, "node", CTLFLAG_RW, 0, "new tree under example");
            
            if (poid == NULL) {
                uprintf("SYSCTL_ADD_NODE failed.\n");
                return EINVAL;
            }

            /*
                SYSCTL_ADD_PROC 的调用创建了一个名为proc的新sysctl，它使用一个函数来处理其读写请求,
                这里函数就是 sysctl_pointless_procedure，仅仅是打印了字符串。这里参数传入的是第二次
                SYSCTL_ADD_NODE 调用的返回值，所以它命名的话要以node的子类型来命名，即 example.node.proc
            */
            SYSCTL_ADD_PROC(&clist, SYSCTL_CHILDREN(poid), OID_AUTO,
                "proc", CTLFLAG_RD, 0, 0, sysctl_pointless_procedure,
                "A", "new proc leaf");

            /*
                这里是 SYSCTL_ADD_NODE 的第三次调用，创建了一个名为example的新子类，传入的参数是 SYSCTL_STATIC_CHILDREN(_debug)，
                所以该子类将以 _debug 为前缀命名， debug.example
                这里，debug是一个静态sysctl顶层类(貌似static children都是顶层类？？)
            */
            poid = SYSCTL_ADD_NODE(&clist, SYSCTL_STATIC_CHILDREN(_debug), OID_AUTO, "example",
                CTLFLAG_RW, 0, "new tree under debug");
            
            if (poid == NULL) {
                uprintf("SYSCTL_ADD_NODE failed.\n");
                return EINVAL;
            }

            /*
                最后再次调用 SYSCTL_ADD_STRING 新建一个名为string的sysctl，以debug_example为前缀命名， debug_example.string
            */
            SYSCTL_ADD_STRING($clist, SYSCTL_CHILDREN(poid), OID_AUTO,
                "string", "CTLFLAG_RD", c, 0, "new string leaf");
            
            uprintf("Pointless module loaded.\n");
            break;
        }
        case MOD_UNLOAD:
        {
            if (sysctl_ctx_free(&clist)) {
                uprintf("sysctl_ctx_free failed.\n");
                return ENOTEMPTY;
            }
            uprintf("Pointless module unloaded.\n");
            break;
        }
        default:
            error = EOPNOTSUPP;
            break;
    }
    return error;
}

static moduledata_t pointless_mod = {
    "pointless",
    pointless_modevent,
    NULL
};

DECLARE_MODULE(pointless, pointless_mod, SI_SUB_EXEC, SI_ORDER_ANY);