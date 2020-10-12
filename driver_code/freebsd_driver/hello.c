#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/system.h>

static int 
hello_modevent(module_t mod_unused, int event, void* arg_unused)
{
    int error = 0;

    switch (event) {
        case MOD_LOAD:
            uprintf("Hello, world!\n");
            break;
        case MOD_UNLOAD:
            uprintf("Good-bye, curel world!\n");
            break;
        default:
            error = EOPNOTSUPP;
            break;
    }

    return error;
}


// typedef struct moduledata {
// 	const char	*name;		/* module name */
// 	modeventhand_t  evhand;		/* event handler 事件处理函数的类型，比如load，unload等等*/
// 	void		*priv;		/* extra data */
// } moduledata_t;

static moduledata_t hello_mod = {
    "hello",
    "hello_modevent",
    NULL
};

/* 
  #define	DECLARE_MODULE(name, data, sub, order)

  parameter：
	name: 	表示模块的名字,用来表示指定的KLD
	data： 	moduledata_t类型的结构体数据，各个名字段都是已经初始化好了的
	sub：	参考enum sysinit_sub_id {}中的具体数值含义，sub总被设置为SI_SUB_DRIVERS
			位置 sys/kernel.h
	order：	制定相应的KLD在子系统sub中的初始化顺序。在 enum sysinit_elem_order {}中
			定义了该参数的合法值，一般设置为 SI_ORDER_MIDDLE，位置 sys/kernel.h
*/

DECLARE_MODULE(hello, hello_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);