/*
 * QEMU Object Model
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_OBJECT_H
#define QEMU_OBJECT_H

#include "qapi/qapi-builtin-types.h"
#include "qemu/module.h"

struct TypeImpl;
typedef struct TypeImpl *Type;

typedef struct TypeInfo TypeInfo;

typedef struct InterfaceClass InterfaceClass;
typedef struct InterfaceInfo InterfaceInfo;

#define TYPE_OBJECT "object"

/**
 * SECTION:object.h
 * @title:Base Object Type System
 * @short_description: interfaces for creating new types and objects
 *
 * The QEMU Object Model provides a framework for registering user creatable
 * types and instantiating objects from those types.  QOM provides the following
 * features:
 *
 *  - System for dynamically registering types
 *  - Support for single-inheritance of types 支持类型的单一继承
 *  - Multiple inheritance of stateless interfaces 无状态接口的多重继承
 *
 * <example>
 *   <title>Creating a minimal type</title>
 *   <programlisting>
 * #include "qdev.h"
 *
 * #define TYPE_MY_DEVICE "my-device"
 *
 * // No new virtual functions: we can reuse the typedef for the
 * // superclass.
 * typedef DeviceClass MyDeviceClass;
 * typedef struct MyDevice
 * {
 *     DeviceState parent;
 *
 *     int reg0, reg1, reg2;
 * } MyDevice;
 *
 * static const TypeInfo my_device_info = {
 *     .name = TYPE_MY_DEVICE,
 *     .parent = TYPE_DEVICE,
 *     .instance_size = sizeof(MyDevice),
 * };
 *
 * static void my_device_register_types(void)
 * {
 *     type_register_static(&my_device_info);
 * }
 *
 * type_init(my_device_register_types)
 *   </programlisting>
 * </example>
 *
 * In the above example, we create a simple type that is described by #TypeInfo.
 * #TypeInfo describes information about the type including what it inherits
 * from, the instance and class size, and constructor/destructor hooks.
 * TypeInfo描述有关类型的信息，包括它继承的内容、实例和类大小以及构造函数/析构函数挂钩
 *
 * Alternatively several static types could be registered using helper macro
 * DEFINE_TYPES()
 *
 * <example>
 *   <programlisting>
 * static const TypeInfo device_types_info[] = {
 *     {
 *         .name = TYPE_MY_DEVICE_A,
 *         .parent = TYPE_DEVICE,
 *         .instance_size = sizeof(MyDeviceA),
 *     },
 *     {
 *         .name = TYPE_MY_DEVICE_B,
 *         .parent = TYPE_DEVICE,
 *         .instance_size = sizeof(MyDeviceB),
 *     },
 * };
 *
 * DEFINE_TYPES(device_types_info)
 *   </programlisting>
 * </example>
 *
 * Every type has an #ObjectClass associated with it.  #ObjectClass derivatives
 * are instantiated dynamically but there is only ever one instance for any
 * given type.  The #ObjectClass typically holds a table of function pointers
 * for the virtual methods implemented by this type.
 * 每种类型都有一个与其关联的ObjectClass。#ObjectClass派生是动态实例化的，但对于任何给定类型，
 * 都只有一个实例。ObjectClass通常保存由该类型实现的虚拟方法的函数指针表
 *
 * Using object_new(), a new #Object derivative will be instantiated.  You can
 * cast an #Object to a subclass (or base-class) type using
 * object_dynamic_cast().  You typically want to define macro wrappers around
 * OBJECT_CHECK() and OBJECT_CLASS_CHECK() to make it easier to convert to a
 * specific type:
 * 使用object_new（），将实例化一个新的#对象派生。可以使用Object_dynamic_cast（）将对象
 * 强制转换为子类（或基类）类型。您通常希望在OBJECT_CHECK（）和OBJECT_CLASS_CHECK（）
 * 周围定义宏包装器，以便更容易地转换为特定类型：
 *
 * <example>
 *   <title>Typecasting macros</title>
 *   <programlisting>
 *    #define MY_DEVICE_GET_CLASS(obj) \
 *       OBJECT_GET_CLASS(MyDeviceClass, obj, TYPE_MY_DEVICE)
 *    #define MY_DEVICE_CLASS(klass) \
 *       OBJECT_CLASS_CHECK(MyDeviceClass, klass, TYPE_MY_DEVICE)
 *    #define MY_DEVICE(obj) \
 *       OBJECT_CHECK(MyDevice, obj, TYPE_MY_DEVICE)
 *   </programlisting>
 * </example>
 *
 * # Class Initialization #
 *
 * Before an object is initialized, the class for the object must be
 * initialized.  There is only one class object for all instance objects
 * that is created lazily.
 * 在初始化对象之前，必须初始化该对象的类。对于延迟创建的所有实例对象，只有一个类对象
 *
 * Classes are initialized by first initializing any parent classes (if
 * necessary).  After the parent class object has initialized, it will be
 * copied into the current class object and any additional storage in the
 * class object is zero filled.
 * 初始化类的方法是首先初始化任何父类（如果需要）。初始化父类对象后，它将被复制到当前类
 * 对象中，并且类对象中的任何附加存储都将被零填充
 *
 * The effect of this is that classes automatically inherit any virtual
 * function pointers that the parent class has already initialized.  All
 * other fields will be zero filled.
 * 这样做的效果是类自动继承父类已经初始化的任何虚函数指针。所有其他字段将为零填充
 *
 * Once all of the parent classes have been initialized, #TypeInfo::class_init
 * is called to let the class being instantiated provide default initialize for
 * its virtual functions.  Here is how the above example might be modified
 * to introduce an overridden virtual function:
 * 初始化所有父类后，将调用#TypeInfo:：class_init，让正在实例化的类为其虚拟函数提供默认初始化；
 * 下面是如何修改上面的示例以引入重写的虚函数
 *
 * <example>
 *   <title>Overriding a virtual function</title>
 *   <programlisting>
 * #include "qdev.h"
 *
 * void my_device_class_init(ObjectClass *klass, void *class_data)
 * {
 *     DeviceClass *dc = DEVICE_CLASS(klass);
 *     dc->reset = my_device_reset;
 * }
 *
 * static const TypeInfo my_device_info = {
 *     .name = TYPE_MY_DEVICE,
 *     .parent = TYPE_DEVICE,
 *     .instance_size = sizeof(MyDevice),
 *     .class_init = my_device_class_init,
 * };
 *   </programlisting>
 * </example>
 *
 * Introducing new virtual methods requires a class to define its own
 * struct and to add a .class_size member to the #TypeInfo.  Each method
 * will also have a wrapper function to call it easily:
 * 引入新的虚拟方法需要一个类来定义它自己的结构，并向#TypeInfo添加一个.class_size成员。
 * 每个方法还将有一个包装器函数以方便调用：
 *
 * <example>
 *   <title>Defining an abstract class</title>
 *   <programlisting>
 * #include "qdev.h"
 *
 * typedef struct MyDeviceClass
 * {
 *     DeviceClass parent;
 *
 *     void (*frobnicate) (MyDevice *obj);
 * } MyDeviceClass;
 *
 * static const TypeInfo my_device_info = {
 *     .name = TYPE_MY_DEVICE,
 *     .parent = TYPE_DEVICE,
 *     .instance_size = sizeof(MyDevice),
 *     .abstract = true, // or set a default in my_device_class_init
 *     .class_size = sizeof(MyDeviceClass),
 * };
 *
 * void my_device_frobnicate(MyDevice *obj)
 * {
 *     MyDeviceClass *klass = MY_DEVICE_GET_CLASS(obj);
 *
 *     klass->frobnicate(obj);
 * }
 *   </programlisting>
 * </example>
 *
 * # Interfaces #
 *
 * Interfaces allow a limited form of multiple inheritance.  Instances are
 * similar to normal types except for the fact that are only defined by
 * their classes and never carry any state.  As a consequence, a pointer to
 * an interface instance should always be of incomplete type in order to be
 * sure it cannot be dereferenced.  That is, you should define the
 * 'typedef struct SomethingIf SomethingIf' so that you can pass around
 * 'SomethingIf *si' arguments, but not define a 'struct SomethingIf { ... }'.
 * The only things you can validly do with a 'SomethingIf *' are to pass it as
 * an argument to a method on its corresponding SomethingIfClass, or to
 * dynamically cast it to an object that implements the interface.
 * 接口允许有限形式的多重继承。实例与普通类型相似，只是实例只由它们的类定义，从不携带任何状态。
 * 因此，指向接口实例的指针应始终为不完整类型，以确保它不能被取消引用。也就是说，应该定义
 * “typedef struct SomethingIf SomethingIf”，这样就可以传递“SomethingIf*si”参数，
 * 但不能定义“struct SomethingIf{。。。}'. 对“SomethingIf*”唯一有效的操作是将其作为
 * 参数传递给其对应的SomethingIf类上的方法，或将其动态转换为实现接口的对象
 *
 * # Methods #
 *
 * A <emphasis>method</emphasis> is a function within the namespace scope of
 * a class. It usually operates on the object instance by passing it as a
 * strongly-typed first argument.
 * If it does not operate on an object instance, it is dubbed
 * <emphasis>class method</emphasis>.
 *
 * Methods cannot be overloaded. That is, the #ObjectClass and method name
 * uniquely identity the function to be called; the signature does not vary
 * except for trailing varargs.
 * 方法不能重载。也就是说，#ObjectClass和方法名唯一标识要调用的函数；除了后面的varargs外，
 * 签名没有变化。
 *
 * Methods are always <emphasis>virtual</emphasis>. Overriding a method in
 * #TypeInfo.class_init of a subclass leads to any user of the class obtained
 * via OBJECT_GET_CLASS() accessing the overridden function.
 * The original function is not automatically invoked. It is the responsibility
 * of the overriding class to determine whether and when to invoke the method
 * being overridden.
 *
 * To invoke the method being overridden, the preferred solution is to store
 * the original value in the overriding class before overriding the method.
 * This corresponds to |[ {super,base}.method(...) ]| in Java and C#
 * respectively; this frees the overriding class from hardcoding its parent
 * class, which someone might choose to change at some point.
 *
 * <example>
 *   <title>Overriding a virtual method</title>
 *   <programlisting>
 * typedef struct MyState MyState;
 *
 * typedef void (*MyDoSomething)(MyState *obj);
 *
 * typedef struct MyClass {
 *     ObjectClass parent_class;
 *
 *     MyDoSomething do_something;
 * } MyClass;
 *
 * static void my_do_something(MyState *obj)
 * {
 *     // do something
 * }
 *
 * static void my_class_init(ObjectClass *oc, void *data)
 * {
 *     MyClass *mc = MY_CLASS(oc);
 *
 *     mc->do_something = my_do_something;
 * }
 *
 * static const TypeInfo my_type_info = {
 *     .name = TYPE_MY,
 *     .parent = TYPE_OBJECT,
 *     .instance_size = sizeof(MyState),
 *     .class_size = sizeof(MyClass),
 *     .class_init = my_class_init,
 * };
 *
 * typedef struct DerivedClass {
 *     MyClass parent_class;
 *
 *     MyDoSomething parent_do_something;
 * } DerivedClass;
 *
 * static void derived_do_something(MyState *obj)
 * {
 *     DerivedClass *dc = DERIVED_GET_CLASS(obj);
 *
 *     // do something here
 *     dc->parent_do_something(obj);
 *     // do something else here
 * }
 *
 * static void derived_class_init(ObjectClass *oc, void *data)
 * {
 *     MyClass *mc = MY_CLASS(oc);
 *     DerivedClass *dc = DERIVED_CLASS(oc);
 *
 *     dc->parent_do_something = mc->do_something;
 *     mc->do_something = derived_do_something;
 * }
 *
 * static const TypeInfo derived_type_info = {
 *     .name = TYPE_DERIVED,
 *     .parent = TYPE_MY,
 *     .class_size = sizeof(DerivedClass),
 *     .class_init = derived_class_init,
 * };
 *   </programlisting>
 * </example>
 *
 * Alternatively, object_class_by_name() can be used to obtain the class and
 * its non-overridden methods for a specific type. This would correspond to
 * |[ MyClass::method(...) ]| in C++.
 *
 * The first example of such a QOM method was #CPUClass.reset,
 * another example is #DeviceClass.realize.
 */


typedef struct ObjectProperty ObjectProperty;

/**
 * ObjectPropertyAccessor:
 * @obj: the object that owns the property
 * @v: the visitor that contains the property data
 * @name: the name of the property
 * @opaque: the object property opaque
 * @errp: a pointer to an Error that is filled if getting/setting fails.
 *
 * Called when trying to get/set a property.
 * 当想要获取属性的时候，可以调用这个函数
 */
typedef void (ObjectPropertyAccessor)(Object *obj,
                                      Visitor *v,
                                      const char *name,
                                      void *opaque,
                                      Error **errp);

/**
 * ObjectPropertyResolve:
 * @obj: the object that owns the property
 * @opaque: the opaque registered with the property
 * @part: the name of the property
 *
 * Resolves the #Object corresponding to property @part.
 * 解析与属性 @part对应的 #object
 *
 * The returned object can also be used as a starting point
 * to resolve a relative path starting with "@part".
 * 返回的对象也可以用作解析以“@part”开头的相对路径的起点
 *
 * Returns: If @path is the path that led to @obj, the function
 * returns the #Object corresponding to "@path/@part".
 * If "@path/@part" is not a valid object path, it returns #NULL.
 */
typedef Object *(ObjectPropertyResolve)(Object *obj,
                                        void *opaque,
                                        const char *part);

/**
 * ObjectPropertyRelease:
 * @obj: the object that owns the property
 * @name: the name of the property
 * @opaque: the opaque registered with the property
 *
 * Called when a property is removed from a object.
 * 当属性从一个object移除的时候，调用这个函数
 */
typedef void (ObjectPropertyRelease)(Object *obj,
                                     const char *name,
                                     void *opaque);

/**
 * ObjectPropertyInit:
 * @obj: the object that owns the property
 * @prop: the property to set
 *
 * Called when a property is initialized.
 * 初始化属性
 */
typedef void (ObjectPropertyInit)(Object *obj, ObjectProperty *prop);

/* 简单理解为名称、类型等基本属性+功能函数 */
struct ObjectProperty
{
    char *name;
    char *type;
    char *description;
    ObjectPropertyAccessor *get;
    ObjectPropertyAccessor *set;
    ObjectPropertyResolve *resolve;
    ObjectPropertyRelease *release;
    ObjectPropertyInit *init;
    void *opaque;
    QObject *defval;
};

/**
 * ObjectUnparent:
 * @obj: the object that is being removed from the composition tree
 * 正在从合成树中删除的对象
 *
 * Called when an object is being removed from the QOM composition tree.
 * The function should remove any backlinks from children objects to @obj.
 * 从QOM组合树中移除对象时调用。函数应该删除子对象到@obj的所有反向链接
 * 
 * QEMU提供了一套面向对象编程的模型——QOM，即QEMU Object Module，几乎所有的设备如CPU、
 * 内存、总线等都是利用这一面向对象的模型来实现的。QOM模型的实现代码位于qom/文件夹下的文
 * 件中;对于开发者而言，只要知道如何利用QOM模型创建类和对象就可以了，但是开发者只有理解了
 * QOM的相关数据结构，才能清楚如何利用QOM模型
 * 
 * 各种架构CPU的模拟和实现
        QEMU中要实现对各种CPU架构的模拟，而且对于一种架构的CPU，比如X86_64架构的CPU，由于
        包含的特性不同，也会有不同的CPU模型。任何CPU中都有CPU通用的属性，同时也包含各自特有
        的属性。为了便于模拟这些CPU模型，面向对象的变成模型是必不可少的。
   模拟device与bus的关系
        在主板上，一个device会通过bus与其他的device相连接，一个device上可以通过不同的bus
        端口连接到其他的device，而其他的device也可以进一步通过bus与其他的设备连接，同时一个
        bus上也可以连接多个device，这种device连bus、bus连device的关系，qemu是需要模拟出
        来的。为了方便模拟设备的这种特性，面向对象的编程模型也是必不可少的。
 */
typedef void (ObjectUnparent)(Object *obj);

/**
 * ObjectFree:
 * @obj: the object being freed
 *
 * Called when an object's last reference is removed.
 */
typedef void (ObjectFree)(void *obj);

#define OBJECT_CLASS_CAST_CACHE 4

/**
 * ObjectClass:
 *
 * The base for all classes.  The only thing that #ObjectClass contains is an
 * integer type handle. 所有class的基类，ObjectClass只包含一个整型handler。
 */
struct ObjectClass
{
    /*< private >*/
    Type type;
    GSList *interfaces;

    const char *object_cast_cache[OBJECT_CLASS_CAST_CACHE];
    const char *class_cast_cache[OBJECT_CLASS_CAST_CACHE];

    ObjectUnparent *unparent;

    GHashTable *properties;
};

/**
 * Object:
 *
 * The base for all objects.  The first member of this object is a pointer to
 * a #ObjectClass.  Since C guarantees that the first member of a structure
 * always begins at byte 0 of that structure, as long as any sub-object places
 * its parent as the first member, we can cast directly to a #Object.
 * 由于C保证结构的第一个成员总是从该结构的字节0开始，只要任何子对象将其父对象作为第一个成员，
 * 我们就可以直接转换为一个#object
 *
 * As a result, #Object contains a reference to the objects type as its
 * first member.  This allows identification of the real type of the object at
 * run time.
 * 因此，#Object包含对objects类型的引用作为其第一个成员。这允许在运行时标识对象的实际类型；
 * 应该指的就是 ObjectClass 用来指示实际的object类型
 */
struct Object
{
    /*< private >*/
    ObjectClass *class;
    ObjectFree *free;       /* 当对象的引用为0时，清理垃圾的回调函数 */
    GHashTable *properties;     /* Hash表记录Object的属性 */
    uint32_t ref;       /* 该对象的引用计数 */
    Object *parent;
};

/**
 * TypeInfo:
 *  TypeInfo：是用户用来定义一个Type的工具型的数据结构，用户定义了一个TypeInfo，然后调用
 * type_register(TypeInfo )或者type_register_static(TypeInfo )函数，就会生成相应的
 * TypeImpl实例，将这个TypeInfo注册到全局的TypeImpl的hash表中
 * 
 * 类型Type，QOM中所有的模型都是通过类型Type注册的，具体实现过程就是从类型信息TypeInfo注册
 * 为类型实现TypeImpl，并把所有类型实现添加到全局的GHashTable表中，hash表以类型Type的名字
 * 作为key键值。类型Type有父子的概念，QOM有对象(TYPE_OBJECT)和接口(TYPE_INTERFACE)两种
 * 根父类型
 * 
 * @name: The name of the type.
 * @parent: The name of the parent type.
 * @instance_size: The size of the object (derivative of #Object).  If
 *   @instance_size is 0, then the size of the object will be the size of the
 *   parent object.
 *   对象的大小（对象的派生）。如果@instance_size为0，则对象的大小将为父对象的大小
 * 
 * @instance_init: This function is called to initialize an object.  The parent
 *   class will have already been initialized so the type is only responsible
 *   for initializing its own members.
 *   初始化调用的时候调用。这个时候可能父类已经初始化过了，所以这里仅仅初始化它自己的成员
 * 
 * @instance_post_init: This function is called to finish initialization of
 *   an object, after all @instance_init functions were called.
 *   当一个object的所有初始化动作完成之后，调用这个函数
 * 
 * @instance_finalize: This function is called during object destruction.  This
 *   is called before the parent @instance_finalize function has been called.
 *   An object should only free the members that are unique to its type in this
 *   function.
 *   当一个object销毁的时候调用这个函数，而且这个要在parent instance_finalize 函数调用
 *   之前调用。在这个函数中，对象只应释放其类型所特有的成员。从父类继承的成员在这里不做处理
 * 
 * @abstract: If this field is true, then the class is considered abstract and
 *   cannot be directly instantiated.
 *   如果此字段为true，则该类被认为是抽象的，不能直接实例化
 * 
 * @class_size: The size of the class object (derivative(派生) of #ObjectClass)
 *   for this object.  If @class_size is 0, then the size of the class will be
 *   assumed to be the size of the parent class.  This allows a type to avoid
 *   implementing an explicit class type if they are not adding additional
 *   virtual functions.
 *   此对象的类对象（ObjectClass的派生）的大小。如果@class_size为0，则类的大小将假定为父类
 *   的大小。这允许类型在不添加额外的虚函数时避免实现显式类类型。
 * 
 * @class_init: This function is called after all parent class initialization
 *   has occurred to allow a class to set its default virtual method pointers.
 *   This is also the function to use to override virtual methods from a parent
 *   class.
 *   在所有父类初始化完成后调用此函数，以允许类设置其默认的虚拟方法指针。这也是用于重写父类中的
 *   虚方法的函数
 * 
 * @class_base_init: This function is called for all base classes after all
 *   parent class initialization has occurred, but before the class itself
 *   is initialized.  This is the function to use to undo the effects of
 *   memcpy from the parent class to the descendants.
 *   在所有父类初始化发生之后，但在类本身初始化之前，将对所有基类调用此函数。此函数用于撤消
 *   memcpy从父类到子类的效果
 * 
 * @class_data: Data to pass to the @class_init,
 *   @class_base_init. This can be useful when building dynamic
 *   classes.
 *   用于class_init 和 class_base_init 这两个函数，并且对于创建动态class是有帮助的
 * 
 * @interfaces: The list of interfaces associated with this type.  This
 *   should point to a static array that's terminated with a zero filled
 *   element.
 *   与此类型关联的接口列表。这应该指向一个以零填充元素结束的静态数组
 */

/*
    每个QOM设备模型类型包括设备，总线，接口等都会定义一个类型信息TypeInfo。类型信息TypeInfo是用于
    注册类型实现TypeImpl的结构模板。TypeInfo通过name定义类型名称，通过parernt指定父类型名称
    也就是说每种类型的设备都是通过 TypeInfo 来进行区分和注册，可以参考一些实例，比如 pci_testdev_info；
    而且 TypeInfo 一般是静态定义的，有时候也会动态生成
*/
struct TypeInfo
{
    const char *name;   //类型名称
    const char *parent; //父类型名称，TYPE_OBJECT 和 TYPE_INTERFACE 是两个根类型的名称

    size_t instance_size;   //对象实例的大小
    void (*instance_init)(Object *obj);  //对象实例的init函数，父类型的init已被执行过了，只需要负责本类型的
    void (*instance_post_init)(Object *obj);    //对象实例的post_init函数，在init执行完后执行
    void (*instance_finalize)(Object *obj); //对象实例销毁时执行，释放动态资源

    bool abstract;  //如为true则是抽象的类型，不能直接实例
    size_t class_size;  //类对象实例的大小

    void (*class_init)(ObjectClass *klass, void *data); //类对象实例的init函数，初始化自己的方法指针，也可覆盖父类的方法指针

    //在父类的class_init执行完，自己的class_init执行之前执行，做一些清理工作
    void (*class_base_init)(ObjectClass *klass, void *data);    
    void *class_data;   //传递给类对象实例各个方法的数据

    InterfaceInfo *interfaces;  //类型定义的接口信息名称数组
};

/**
 * OBJECT:
 * @obj: A derivative of #Object - object派生
 *
 * Converts an object to a #Object.  Since all objects are #Objects,
 * this function will always succeed.
 * 应该就是将一种object派生出来的类型转换成object类型
 */
#define OBJECT(obj) \
    ((Object *)(obj))

/**
 * OBJECT_CLASS:
 * @class: A derivative of #ObjectClass.
 *
 * Converts a class to an #ObjectClass.  Since all objects are #Objects,
 * this function will always succeed.
 */
#define OBJECT_CLASS(class) \
    ((ObjectClass *)(class))

/**
 * OBJECT_CHECK:
 * @type: The C type to use for the return value.
 * @obj: A derivative of @type to cast.
 * @name: The QOM typename of @type
 *
 * A type safe version of @object_dynamic_cast_assert.  Typically each class
 * will define a macro based on this type to perform type safe dynamic_casts to
 * this object type.
 * @object_dynamic_cast_assert的类型安全版本。通常每个类都将基于此类型定义一个宏，以对该
 * 对象类型执行类型安全的动态转换。这个可能就是用来判断我们所要执行的类型转换是不是安全的，如果
 * 不是安全操作，那么调用这个函数后就会报错；这里针对的是object
 *
 * If an invalid object is passed to this function, a run time assert will be
 * generated.
 */
#define OBJECT_CHECK(type, obj, name) \
    ((type *)object_dynamic_cast_assert(OBJECT(obj), (name), \
                                        __FILE__, __LINE__, __func__))

/**
 * OBJECT_CLASS_CHECK:
 * @class_type: The C type to use for the return value.
 * @class: A derivative class of @class_type to cast.
 * @name: the QOM typename of @class_type.
 *
 * A type safe version of @object_class_dynamic_cast_assert.  This macro is
 * typically wrapped by each type to perform type safe casts of a class to a
 * specific class type.
 * 作用同上，只不过这里针对的类型是object_class
 */
#define OBJECT_CLASS_CHECK(class_type, class, name) \
    ((class_type *)object_class_dynamic_cast_assert(OBJECT_CLASS(class), (name), \
                                               __FILE__, __LINE__, __func__))

/**
 * OBJECT_GET_CLASS:
 * @class: The C type to use for the return value.
 * @obj: The object to obtain the class for.
 * @name: The QOM typename of @obj.
 *
 * This function will return a specific class for a given object.  Its generally
 * used by each type to provide a type safe macro to get a specific class type
 * from an object.
 * 此函数将返回给定对象的特定类。它通常被每个类型用来提供一个类型安全宏，以从对象中获取特定的类类型
 */
#define OBJECT_GET_CLASS(class, obj, name) \
    OBJECT_CLASS_CHECK(class, object_get_class(OBJECT(obj)), name)

/**
 * InterfaceInfo:
 * @type: The name of the interface.
 *
 * The information associated with an interface.
 * 仅仅显示了interface的名称，并且关联到一个interface
 */
struct InterfaceInfo {
    const char *type;
};

/**
 * InterfaceClass:
 * @parent_class: the base class
 *
 * The class for all interfaces.  Subclasses of this class should only add
 * virtual methods. 所有接口的类。此类的子类只应添加虚拟方法。
 */
struct InterfaceClass
{
    ObjectClass parent_class;
    /*< private >*/
    ObjectClass *concrete_class;    /* 可继承类？？ */
    Type interface_type;
};

#define TYPE_INTERFACE "interface"

/**
 * INTERFACE_CLASS:
 * @klass: class to cast from
 * Returns: An #InterfaceClass or raise an error if cast is invalid
 * 检查类型转换的安全性(interface class)
 */
#define INTERFACE_CLASS(klass) \
    OBJECT_CLASS_CHECK(InterfaceClass, klass, TYPE_INTERFACE)

/**
 * INTERFACE_CHECK:
 * @interface: the type to return
 * @obj: the object to convert to an interface
 * @name: the interface type name
 *
 * Returns: @obj casted to @interface if cast is valid, otherwise raise error.
 */
#define INTERFACE_CHECK(interface, obj, name) \
    ((interface *)object_dynamic_cast_assert(OBJECT((obj)), (name), \
                                             __FILE__, __LINE__, __func__))

/**
 * object_new_with_class:
 * @klass: The class to instantiate.
 *
 * This function will initialize a new object using heap allocated memory.
 * The returned object has a reference count of 1, and will be freed when
 * the last reference is dropped.
 * 此函数将使用堆分配的内存初始化新对象。返回的对象的引用计数为1，将在删除最后一个引用
 * 时释放该对象
 *
 * Returns: The newly allocated and instantiated object.
 */
Object *object_new_with_class(ObjectClass *klass);

/**
 * object_new:
 * @typename: The name of the type of the object to instantiate.
 * 要实例化的对象的类型的名称
 *
 * This function will initialize a new object using heap allocated memory.
 * The returned object has a reference count of 1, and will be freed when
 * the last reference is dropped.
 *
 * Returns: The newly allocated and instantiated object.
 */
Object *object_new(const char *typename);

/**
 * object_new_with_props:
 * @typename:  The name of the type of the object to instantiate.
 * @parent: the parent object
 * @id: The unique ID of the object
 * @errp: pointer to error object
 * @...: list of property names and values
 * 实例化带有属性信息的object
 *
 * This function will initialize a new object using heap allocated memory.
 * The returned object has a reference count of 1, and will be freed when
 * the last reference is dropped.
 *
 * The @id parameter will be used when registering the object as a
 * child of @parent in the composition tree.
 * 在组合树中将对象注册为@parent的子对象时，将使用@id参数
 *
 * The variadic parameters are a list of pairs of (propname, propvalue)
 * strings. The propname of %NULL indicates the end of the property
 * list. If the object implements the user creatable interface, the
 * object will be marked complete once all the properties have been
 * processed.
 * 可变参数是（propname，propvalue）字符串对的列表。propname%NULL表示属性列表
 * 的结尾。如果对象实现了用户可创建的接口，那么在处理完所有属性之后，该对象将被标记
 * 为完成
 *
 * <example>
 *   <title>Creating an object with properties</title>
 *   <programlisting>
 *   Error *err = NULL;
 *   Object *obj;
 *
 *   obj = object_new_with_props(TYPE_MEMORY_BACKEND_FILE,
 *                               object_get_objects_root(),
 *                               "hostmem0",
 *                               &err,
 *                               "share", "yes",
 *                               "mem-path", "/dev/shm/somefile",
 *                               "prealloc", "yes",
 *                               "size", "1048576",
 *                               NULL);
 *
 *   if (!obj) {
 *     error_reportf_err(err, "Cannot create memory backend: ");
 *   }
 *   </programlisting>
 * </example>
 *
 * The returned object will have one stable reference maintained
 * for as long as it is present in the object hierarchy.
 * 返回的对象将有一个稳定的引用，只要它存在于对象层次结构中
 *
 * Returns: The newly allocated, instantiated & initialized object.
 */
Object *object_new_with_props(const char *typename,
                              Object *parent,
                              const char *id,
                              Error **errp,
                              ...) QEMU_SENTINEL;

/**
 * object_new_with_propv:
 * @typename:  The name of the type of the object to instantiate.
 * @parent: the parent object
 * @id: The unique ID of the object
 * @errp: pointer to error object
 * @vargs: list of property names and values
 *
 * See object_new_with_props() for documentation.
 * 作用同上
 */
Object *object_new_with_propv(const char *typename,
                              Object *parent,
                              const char *id,
                              Error **errp,
                              va_list vargs);

bool object_apply_global_props(Object *obj, const GPtrArray *props,
                               Error **errp);
void object_set_machine_compat_props(GPtrArray *compat_props);
void object_set_accelerator_compat_props(GPtrArray *compat_props);
void object_register_sugar_prop(const char *driver, const char *prop, const char *value);
void object_apply_compat_props(Object *obj);

/**
 * object_set_props:
 * @obj: the object instance to set properties on
 * @errp: pointer to error object
 * @...: list of property names and values
 *
 * This function will set a list of properties on an existing object
 * instance.    此函数用于设置现有对象实例的属性列表
 *
 * The variadic parameters are a list of pairs of (propname, propvalue)
 * strings. The propname of %NULL indicates the end of the property
 * list.    属性是按照map类型的格式出现的，以NULL作为结尾
 *
 * <example>
 *   <title>Update an object's properties</title>
 *   <programlisting>
 *   Error *err = NULL;
 *   Object *obj = ...get / create object...;
 *
 *   if (!object_set_props(obj,
 *                         &err,
 *                         "share", "yes",
 *                         "mem-path", "/dev/shm/somefile",
 *                         "prealloc", "yes",
 *                         "size", "1048576",
 *                         NULL)) {
 *     error_reportf_err(err, "Cannot set properties: ");
 *   }
 *   </programlisting>
 * </example>
 *
 * The returned object will have one stable reference maintained
 * for as long as it is present in the object hierarchy.
 *
 * Returns: %true on success, %false on error.
 */
bool object_set_props(Object *obj, Error **errp, ...) QEMU_SENTINEL;

/**
 * object_set_propv:
 * @obj: the object instance to set properties on
 * @errp: pointer to error object
 * @vargs: list of property names and values
 *
 * See object_set_props() for documentation.
 *
 * Returns: %true on success, %false on error.
 * 作用同上
 */
bool object_set_propv(Object *obj, Error **errp, va_list vargs);

/**
 * object_initialize:
 * @obj: A pointer to the memory to be used for the object.
 * @size: The maximum size available at @obj for the object.
 * @typename: The name of the type of the object to instantiate.
 *
 * This function will initialize an object.  The memory for the object should
 * have already been allocated.  The returned object has a reference count of 1,
 * and will be finalized when the last reference is dropped.
 * 用于已经分配内存的object
 */
void object_initialize(void *obj, size_t size, const char *typename);

/**
 * object_initialize_child_with_props:
 * @parentobj: The parent object to add a property to
 * @propname: The name of the property
 * @childobj: A pointer to the memory to be used for the object.
 * @size: The maximum size available at @childobj for the object.
 * @type: The name of the type of the object to instantiate.
 * @errp: If an error occurs, a pointer to an area to store the error
 * @...: list of property names and values
 *
 * This function will initialize an object. The memory for the object should
 * have already been allocated. The object will then be added as child property
 * to a parent with object_property_add_child() function. The returned object
 * has a reference count of 1 (for the "child<...>" property from the parent),
 * so the object will be finalized automatically when the parent gets removed.
 * 
 * 此函数将初始化一个对象。对象的内存应该已经分配了。然后，该对象将作为子属性添加到父级中，通过
 * object_property_add_child（）函数。返回的对象的引用计数为1（对于父对象的“child<…>”属性），
 * 因此当父对象被删除时，该对象将自动完成释放
 *
 * The variadic parameters are a list of pairs of (propname, propvalue)
 * strings. The propname of %NULL indicates the end of the property list.
 * If the object implements the user creatable interface, the object will
 * be marked complete once all the properties have been processed.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_initialize_child_with_props(Object *parentobj,
                             const char *propname,
                             void *childobj, size_t size, const char *type,
                             Error **errp, ...) QEMU_SENTINEL;

/**
 * object_initialize_child_with_propsv:
 * @parentobj: The parent object to add a property to
 * @propname: The name of the property
 * @childobj: A pointer to the memory to be used for the object.
 * @size: The maximum size available at @childobj for the object.
 * @type: The name of the type of the object to instantiate.
 * @errp: If an error occurs, a pointer to an area to store the error
 * @vargs: list of property names and values
 *
 * See object_initialize_child() for documentation.
 * 作用同上
 * 
 * Returns: %true on success, %false on failure.
 */
bool object_initialize_child_with_propsv(Object *parentobj,
                              const char *propname,
                              void *childobj, size_t size, const char *type,
                              Error **errp, va_list vargs);

/**
 * object_initialize_child:
 * @parent: The parent object to add a property to
 * @propname: The name of the property
 * @child: A precisely typed pointer to the memory to be used for the
 * object.
 * @type: The name of the type of the object to instantiate.
 *
 * This is like
 * object_initialize_child_with_props(parent, propname,
 *                                    child, sizeof(*child), type,
 *                                    &error_abort, NULL)
 */
#define object_initialize_child(parent, propname, child, type)          \
    object_initialize_child_internal((parent), (propname),              \
                                     (child), sizeof(*(child)), (type))
void object_initialize_child_internal(Object *parent, const char *propname,
                                      void *child, size_t size,
                                      const char *type);

/**
 * object_dynamic_cast:
 * @obj: The object to cast.
 * @typename: The @typename to cast to.
 *
 * This function will determine if @obj is-a @typename.  @obj can refer to an
 * object or an interface associated with an object.
 *
 * Returns: This function returns @obj on success or #NULL on failure.
 */
Object *object_dynamic_cast(Object *obj, const char *typename); /* 类型check */

/**
 * object_dynamic_cast_assert:
 *
 * See object_dynamic_cast() for a description of the parameters of this
 * function.  The only difference in behavior is that this function asserts
 * instead of returning #NULL on failure if QOM cast debugging is enabled.
 * This function is not meant to be called directly, but only through
 * the wrapper macro OBJECT_CHECK.
 */
Object *object_dynamic_cast_assert(Object *obj, const char *typename,
                                   const char *file, int line, const char *func);

/**
 * object_get_class:
 * @obj: A derivative of #Object - 派生类(获取派生类的ObjectClass)
 *
 * Returns: The #ObjectClass of the type associated with @obj.
 */
ObjectClass *object_get_class(Object *obj);

/**
 * object_get_typename:
 * @obj: A derivative of #Object. 也是针对派生类对象的
 *
 * Returns: The QOM typename of @obj.
 */
const char *object_get_typename(const Object *obj);

/**
 * type_register_static:
 * @info: The #TypeInfo of the new type. 添加了新的TypeInfo
 *
 * @info and all of the strings it points to should exist for the life time
 * that the type is registered.
 * @info及其指向的所有字符串应该在注册类型的生命周期内存在
 *
 * Returns: the new #Type.
 */
Type type_register_static(const TypeInfo *info);

/**
 * type_register:
 * @info: The #TypeInfo of the new type
 *
 * Unlike type_register_static(), this call does not require @info or its
 * string members to continue to exist after the call returns.
 * 与 type_register_static() 不同，此调用不需要@info或其字符串成员在调用返回后继续存在
 *
 * Returns: the new #Type.
 */
Type type_register(const TypeInfo *info);

/**
 * type_register_static_array:
 * @infos: The array of the new type #TypeInfo structures.
 * @nr_infos: number of entries in @infos
 *
 * @infos and all of the strings it points to should exist for the life time
 * that the type is registered.
 */
void type_register_static_array(const TypeInfo *infos, int nr_infos);

/**
 * DEFINE_TYPES:
 * @type_array: The array containing #TypeInfo structures to register
 *
 * @type_array should be static constant that exists for the life time
 * that the type is registered.
 */
#define DEFINE_TYPES(type_array)                                            \
static void do_qemu_init_ ## type_array(void)                               \
{                                                                           \
    type_register_static_array(type_array, ARRAY_SIZE(type_array));         \
}                                                                           \
type_init(do_qemu_init_ ## type_array)

/**
 * object_class_dynamic_cast_assert:
 * @klass: The #ObjectClass to attempt to cast.
 * @typename: The QOM typename of the class to cast to.
 *
 * See object_class_dynamic_cast() for a description of the parameters
 * of this function.  The only difference in behavior is that this function
 * asserts instead of returning #NULL on failure if QOM cast debugging is
 * enabled.  This function is not meant to be called directly, but only through
 * the wrapper macros OBJECT_CLASS_CHECK and INTERFACE_CHECK.
 */
ObjectClass *object_class_dynamic_cast_assert(ObjectClass *klass,
                                              const char *typename,
                                              const char *file, int line,
                                              const char *func);

/**
 * object_class_dynamic_cast:
 * @klass: The #ObjectClass to attempt to cast.
 * @typename: The QOM typename of the class to cast to.
 *
 * Returns: If @typename is a class, this function returns @klass if
 * @typename is a subtype of @klass, else returns #NULL.
 *
 * If @typename is an interface, this function returns the interface
 * definition for @klass if @klass implements it unambiguously; #NULL
 * is returned if @klass does not implement the interface or if multiple
 * classes or interfaces on the hierarchy leading to @klass implement
 * it.  (FIXME: perhaps this can be detected at type definition time?)
 */
ObjectClass *object_class_dynamic_cast(ObjectClass *klass,
                                       const char *typename);

/**
 * object_class_get_parent:
 * @klass: The class to obtain the parent for.
 * 要为其获取父级的类，文件中的结构体好像都是这种存在继承关系的
 *
 * Returns: The parent for @klass or %NULL if none.
 */
ObjectClass *object_class_get_parent(ObjectClass *klass);

/**
 * object_class_get_name:
 * @klass: The class to obtain the QOM typename for.
 *
 * Returns: The QOM typename for @klass.
 */
const char *object_class_get_name(ObjectClass *klass);

/**
 * object_class_is_abstract:
 * @klass: The class to obtain the abstractness for.
 * 判断是不是抽象类型
 * Returns: %true if @klass is abstract, %false otherwise.
 */
bool object_class_is_abstract(ObjectClass *klass);

/**
 * object_class_by_name:
 * @typename: The QOM typename to obtain the class for.
 *
 * Returns: The class for @typename or %NULL if not found.
 */
ObjectClass *object_class_by_name(const char *typename);

/**
 * module_object_class_by_name:
 * @typename: The QOM typename to obtain the class for.
 *
 * For objects which might be provided by a module.  Behaves like
 * object_class_by_name, but additionally tries to load the module
 * needed in case the class is not available.
 * 对于可能由模块提供的对象。其行为类似于object_class_by_name，但如果类不可用，
 * 则会尝试加载所需的模块
 *
 * Returns: The class for @typename or %NULL if not found.
 */
ObjectClass *module_object_class_by_name(const char *typename);

void object_class_foreach(void (*fn)(ObjectClass *klass, void *opaque),
                          const char *implements_type, bool include_abstract,
                          void *opaque);

/**
 * object_class_get_list:
 * @implements_type: The type to filter for, including its derivatives.
 * 要筛选的类型，包括其派生类型
 * 
 * @include_abstract: Whether to include abstract classes.
 *
 * Returns: A singly-linked list of the classes in reverse hashtable order.
 * 按哈希表顺序反向排列的类的单链链表
 */
GSList *object_class_get_list(const char *implements_type,
                              bool include_abstract);

/**
 * object_class_get_list_sorted:
 * @implements_type: The type to filter for, including its derivatives.
 * @include_abstract: Whether to include abstract classes.
 *
 * Returns: A singly-linked list of the classes in alphabetical
 * case-insensitive order. 按字母不区分大小写顺序排列的类的单链列表
 */
GSList *object_class_get_list_sorted(const char *implements_type,
                              bool include_abstract);

/**
 * object_ref:
 * @obj: the object
 *
 * Increase the reference count of a object.  A object cannot be freed as long
 * as its reference count is greater than zero.
 * 添加object的引用值，只要不为0，就不能被释放
 * Returns: @obj
 */
Object *object_ref(Object *obj);

/**
 * object_unref:
 * @obj: the object
 *
 * Decrease the reference count of a object.  A object cannot be freed as long
 * as its reference count is greater than zero.
 */
void object_unref(Object *obj);

/**
 * object_property_try_add:
 * @obj: the object to add a property to
 * @name: the name of the property.  This can contain any character except for
 *  a forward slash.  In general, you should use hyphens '-' instead of
 *  underscores '_' when naming properties. 属性的名称
 * @type: the type name of the property.  This namespace is pretty loosely
 *   defined.  Sub namespaces are constructed by using a prefix and then
 *   to angle brackets.  For instance, the type 'virtio-net-pci' in the
 *   'link' namespace would be 'link<virtio-net-pci>'. 属性的类型名称
 * @get: The getter to be called to read a property.  If this is NULL, then
 *   the property cannot be read.
 * @set: the setter to be called to write a property.  If this is NULL,
 *   then the property cannot be written.
 * @release: called when the property is removed from the object.  This is
 *   meant to allow a property to free its opaque upon object
 *   destruction.  This may be NULL.
 * @opaque: an opaque pointer to pass to the callbacks for the property
 *          传递到属性的回调的不透明指针
 * @errp: pointer to error object
 *
 * Returns: The #ObjectProperty; this can be used to set the @resolve
 * callback for child and link properties.
 */
ObjectProperty *object_property_try_add(Object *obj, const char *name,
                                        const char *type,
                                        ObjectPropertyAccessor *get,
                                        ObjectPropertyAccessor *set,
                                        ObjectPropertyRelease *release,
                                        void *opaque, Error **errp);

/**
 * object_property_add:
 * Same as object_property_try_add() with @errp hardcoded to
 * &error_abort.
 */
ObjectProperty *object_property_add(Object *obj, const char *name,
                                    const char *type,
                                    ObjectPropertyAccessor *get,
                                    ObjectPropertyAccessor *set,
                                    ObjectPropertyRelease *release,
                                    void *opaque);

/* property delete */
void object_property_del(Object *obj, const char *name);

/* 前面是对object添加属性，这里是对object_class添加属性 */
ObjectProperty *object_class_property_add(ObjectClass *klass, const char *name,
                                          const char *type,
                                          ObjectPropertyAccessor *get,
                                          ObjectPropertyAccessor *set,
                                          ObjectPropertyRelease *release,
                                          void *opaque);

/**
 * object_property_set_default_bool:
 * @prop: the property to set
 * @value: the value to be written to the property
 *
 * Set the property default value. 将属性设置为默认值
 */
void object_property_set_default_bool(ObjectProperty *prop, bool value);

/**
 * object_property_set_default_str:
 * @prop: the property to set
 * @value: the value to be written to the property
 *
 * Set the property default value.  前面是bool类型，这里是string类型
 */
void object_property_set_default_str(ObjectProperty *prop, const char *value);

/**
 * object_property_set_default_int:
 * @prop: the property to set
 * @value: the value to be written to the property
 *
 * Set the property default value.
 */
void object_property_set_default_int(ObjectProperty *prop, int64_t value);

/**
 * object_property_set_default_uint:
 * @prop: the property to set
 * @value: the value to be written to the property
 *
 * Set the property default value.
 */
void object_property_set_default_uint(ObjectProperty *prop, uint64_t value);

/**
 * object_property_find:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Look up a property for an object and return its #ObjectProperty if found.
 */
ObjectProperty *object_property_find(Object *obj, const char *name,
                                     Error **errp);
ObjectProperty *object_class_property_find(ObjectClass *klass, const char *name,
                                           Error **errp);

/* 类似与C++迭代器 */
typedef struct ObjectPropertyIterator {
    ObjectClass *nextclass;
    GHashTableIter iter;
} ObjectPropertyIterator;

/**
 * object_property_iter_init:
 * @obj: the object
 *
 * Initializes an iterator for traversing all properties
 * registered against an object instance, its class and all parent classes.
 *
 * It is forbidden to modify the property list while iterating,
 * whether removing or adding properties.
 *
 * Typical usage pattern would be
 *
 * <example>
 *   <title>Using object property iterators</title>
 *   <programlisting>
 *   ObjectProperty *prop;
 *   ObjectPropertyIterator iter;
 *
 *   object_property_iter_init(&iter, obj);
 *   while ((prop = object_property_iter_next(&iter))) {
 *     ... do something with prop ...
 *   }
 *   </programlisting>
 * </example>
 */
void object_property_iter_init(ObjectPropertyIterator *iter,
                               Object *obj);

/**
 * object_class_property_iter_init:
 * @klass: the class
 *
 * Initializes an iterator for traversing all properties
 * registered against an object class and all parent classes.
 *
 * It is forbidden to modify the property list while iterating,
 * whether removing or adding properties.
 *
 * This can be used on abstract classes as it does not create a temporary
 * instance.
 */
void object_class_property_iter_init(ObjectPropertyIterator *iter,
                                     ObjectClass *klass);

/**
 * object_property_iter_next:
 * @iter: the iterator instance
 *
 * Return the next available property. If no further properties
 * are available, a %NULL value will be returned and the @iter
 * pointer should not be used again after this point without
 * re-initializing it.
 *
 * Returns: the next property, or %NULL when all properties
 * have been traversed.
 */
ObjectProperty *object_property_iter_next(ObjectPropertyIterator *iter);

void object_unparent(Object *obj);

/**
 * object_property_get:
 * @obj: the object
 * @name: the name of the property
 * @v: the visitor that will receive the property value.  This should be an
 *   Output visitor and the data will be written with @name as the name.
 * @errp: returns an error if this function fails
 *
 * Reads a property from a object.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_get(Object *obj, const char *name, Visitor *v,
                         Error **errp);

/**
 * object_property_set_str:
 * @name: the name of the property
 * @value: the value to be written to the property
 * @errp: returns an error if this function fails
 *
 * Writes a string value to a property.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_set_str(Object *obj, const char *name,
                             const char *value, Error **errp);

/**
 * object_property_get_str:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to a C string, or NULL if
 * an error occurs (including when the property value is not a string).
 * The caller should free the string.
 */
char *object_property_get_str(Object *obj, const char *name,
                              Error **errp);

/**
 * object_property_set_link:
 * @name: the name of the property
 * @value: the value to be written to the property
 * @errp: returns an error if this function fails
 *
 * Writes an object's canonical path to a property.
 *
 * If the link property was created with
 * <code>OBJ_PROP_LINK_STRONG</code> bit, the old target object is
 * unreferenced, and a reference is added to the new target object.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_set_link(Object *obj, const char *name,
                              Object *value, Error **errp);

/**
 * object_property_get_link:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, resolved from a path to an Object,
 * or NULL if an error occurs (including when the property value is not a
 * string or not a valid object path).
 */
Object *object_property_get_link(Object *obj, const char *name,
                                 Error **errp);

/**
 * object_property_set_bool:
 * @name: the name of the property
 * @value: the value to be written to the property
 * @errp: returns an error if this function fails
 *
 * Writes a bool value to a property.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_set_bool(Object *obj, const char *name,
                              bool value, Error **errp);

/**
 * object_property_get_bool:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to a boolean, or NULL if
 * an error occurs (including when the property value is not a bool).
 */
bool object_property_get_bool(Object *obj, const char *name,
                              Error **errp);

/**
 * object_property_set_int:
 * @name: the name of the property
 * @value: the value to be written to the property
 * @errp: returns an error if this function fails
 *
 * Writes an integer value to a property.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_set_int(Object *obj, const char *name,
                             int64_t value, Error **errp);

/**
 * object_property_get_int:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to an integer, or negative if
 * an error occurs (including when the property value is not an integer).
 */
int64_t object_property_get_int(Object *obj, const char *name,
                                Error **errp);

/**
 * object_property_set_uint:
 * @name: the name of the property
 * @value: the value to be written to the property
 * @errp: returns an error if this function fails
 *
 * Writes an unsigned integer value to a property.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_set_uint(Object *obj, const char *name,
                              uint64_t value, Error **errp);

/**
 * object_property_get_uint:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to an unsigned integer, or 0
 * an error occurs (including when the property value is not an integer).
 */
uint64_t object_property_get_uint(Object *obj, const char *name,
                                  Error **errp);

/**
 * object_property_get_enum:
 * @obj: the object
 * @name: the name of the property
 * @typename: the name of the enum data type
 * @errp: returns an error if this function fails
 *
 * Returns: the value of the property, converted to an integer, or
 * undefined if an error occurs (including when the property value is not
 * an enum).
 */
int object_property_get_enum(Object *obj, const char *name,
                             const char *typename, Error **errp);

/**
 * object_property_set:
 * @obj: the object
 * @name: the name of the property
 * @v: the visitor that will be used to write the property value.  This should
 *   be an Input visitor and the data will be first read with @name as the
 *   name and then written as the property value.
 * @errp: returns an error if this function fails
 *
 * Writes a property to a object.
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_set(Object *obj, const char *name, Visitor *v,
                         Error **errp);

/**
 * object_property_parse:
 * @obj: the object
 * @name: the name of the property
 * @string: the string that will be used to parse the property value.
 * @errp: returns an error if this function fails
 *
 * Parses a string and writes the result into a property of an object.
 * 解析字符串并将结果写入对象的属性
 *
 * Returns: %true on success, %false on failure.
 */
bool object_property_parse(Object *obj, const char *name,
                           const char *string, Error **errp);

/**
 * object_property_print:
 * @obj: the object
 * @name: the name of the property
 * @human: if true, print for human consumption
 * @errp: returns an error if this function fails
 *
 * Returns a string representation of the value of the property.  The
 * caller shall free the string.
 */
char *object_property_print(Object *obj, const char *name, bool human,
                            Error **errp);

/**
 * object_property_get_type:
 * @obj: the object
 * @name: the name of the property
 * @errp: returns an error if this function fails
 *
 * Returns:  The type name of the property.
 */
const char *object_property_get_type(Object *obj, const char *name,
                                     Error **errp);

/**
 * object_get_root:
 *
 * Returns: the root object of the composition tree
 */
Object *object_get_root(void);


/**
 * object_get_objects_root:
 *
 * Get the container object that holds user created
 * object instances. This is the object at path
 * "/objects"
 *
 * Returns: the user object container
 */
Object *object_get_objects_root(void);

/**
 * object_get_internal_root:
 *
 * Get the container object that holds internally used object
 * instances.  Any object which is put into this container must not be
 * user visible, and it will not be exposed in the QOM tree.
 *
 * Returns: the internal object container
 */
Object *object_get_internal_root(void);

/**
 * object_get_canonical_path_component:
 *
 * Returns: The final component in the object's canonical path.  The canonical
 * path is the path within the composition tree starting from the root.
 * %NULL if the object doesn't have a parent (and thus a canonical path).
 */
const char *object_get_canonical_path_component(const Object *obj);

/**
 * object_get_canonical_path:
 *
 * Returns: The canonical path for a object, newly allocated.  This is
 * the path within the composition tree starting from the root.  Use
 * g_free() to free it.
 */
char *object_get_canonical_path(const Object *obj);

/**
 * object_resolve_path:
 * @path: the path to resolve
 * @ambiguous: returns true if the path resolution failed because of an
 *   ambiguous match
 *
 * There are two types of supported paths--absolute paths and partial paths.
 * 
 * Absolute paths are derived from the root object and can follow child<> or
 * link<> properties.  Since they can follow link<> properties, they can be
 * arbitrarily long.  Absolute paths look like absolute filenames and are
 * prefixed with a leading slash.
 * 
 * Partial paths look like relative filenames.  They do not begin with a
 * prefix.  The matching rules for partial paths are subtle but designed to make
 * specifying objects easy.  At each level of the composition tree, the partial
 * path is matched as an absolute path.  The first match is not returned.  At
 * least two matches are searched for.  A successful result is only returned if
 * only one match is found.  If more than one match is found, a flag is
 * returned to indicate that the match was ambiguous.
 *
 * Returns: The matched object or NULL on path lookup failure.
 */
Object *object_resolve_path(const char *path, bool *ambiguous);

/**
 * object_resolve_path_type:
 * @path: the path to resolve
 * @typename: the type to look for.
 * @ambiguous: returns true if the path resolution failed because of an
 *   ambiguous match
 *
 * This is similar to object_resolve_path.  However, when looking for a
 * partial path only matches that implement the given type are considered.
 * This restricts the search and avoids spuriously flagging matches as
 * ambiguous.
 *
 * For both partial and absolute paths, the return value goes through
 * a dynamic cast to @typename.  This is important if either the link,
 * or the typename itself are of interface types.
 *
 * Returns: The matched object or NULL on path lookup failure.
 */
Object *object_resolve_path_type(const char *path, const char *typename,
                                 bool *ambiguous);

/**
 * object_resolve_path_component:
 * @parent: the object in which to resolve the path
 * @part: the component to resolve.
 *
 * This is similar to object_resolve_path with an absolute path, but it
 * only resolves one element (@part) and takes the others from @parent.
 *
 * Returns: The resolved object or NULL on path lookup failure.
 */
Object *object_resolve_path_component(Object *parent, const char *part);

/**
 * object_property_try_add_child:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @child: the child object
 * @errp: pointer to error object
 *
 * Child properties form the composition tree.  All objects need to be a child
 * of another object.  Objects can only be a child of one object.
 *
 * There is no way for a child to determine what its parent is.  It is not
 * a bidirectional relationship.  This is by design.
 *
 * The value of a child property as a C string will be the child object's
 * canonical path. It can be retrieved using object_property_get_str().
 * The child object itself can be retrieved using object_property_get_link().
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_try_add_child(Object *obj, const char *name,
                                              Object *child, Error **errp);

/**
 * object_property_add_child:
 * Same as object_property_try_add_child() with @errp hardcoded to
 * &error_abort
 */
ObjectProperty *object_property_add_child(Object *obj, const char *name,
                                          Object *child);

typedef enum {
    /* Unref the link pointer when the property is deleted */
    OBJ_PROP_LINK_STRONG = 0x1,

    /* private */
    OBJ_PROP_LINK_DIRECT = 0x2,
    OBJ_PROP_LINK_CLASS = 0x4,
} ObjectPropertyLinkFlags;

/**
 * object_property_allow_set_link:
 *
 * The default implementation of the object_property_add_link() check()
 * callback function.  It allows the link property to be set and never returns
 * an error.
 */
void object_property_allow_set_link(const Object *, const char *,
                                    Object *, Error **);

/**
 * object_property_add_link:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @type: the qobj type of the link
 * @targetp: a pointer to where the link object reference is stored
 * @check: callback to veto setting or NULL if the property is read-only
 * @flags: additional options for the link
 *
 * Links establish relationships between objects.  Links are unidirectional
 * although two links can be combined to form a bidirectional relationship
 * between objects.
 *
 * Links form the graph in the object model.
 *
 * The <code>@check()</code> callback is invoked when
 * object_property_set_link() is called and can raise an error to prevent the
 * link being set.  If <code>@check</code> is NULL, the property is read-only
 * and cannot be set.
 *
 * Ownership of the pointer that @child points to is transferred to the
 * link property.  The reference count for <code>*@child</code> is
 * managed by the property from after the function returns till the
 * property is deleted with object_property_del().  If the
 * <code>@flags</code> <code>OBJ_PROP_LINK_STRONG</code> bit is set,
 * the reference count is decremented when the property is deleted or
 * modified.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_link(Object *obj, const char *name,
                              const char *type, Object **targetp,
                              void (*check)(const Object *obj, const char *name,
                                            Object *val, Error **errp),
                              ObjectPropertyLinkFlags flags);

ObjectProperty *object_class_property_add_link(ObjectClass *oc,
                              const char *name,
                              const char *type, ptrdiff_t offset,
                              void (*check)(const Object *obj, const char *name,
                                            Object *val, Error **errp),
                              ObjectPropertyLinkFlags flags);

/**
 * object_property_add_str:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @get: the getter or NULL if the property is write-only.  This function must
 *   return a string to be freed by g_free().
 * @set: the setter or NULL if the property is read-only
 *
 * Add a string property using getters/setters.  This function will add a
 * property of type 'string'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_str(Object *obj, const char *name,
                             char *(*get)(Object *, Error **),
                             void (*set)(Object *, const char *, Error **));

ObjectProperty *object_class_property_add_str(ObjectClass *klass,
                                   const char *name,
                                   char *(*get)(Object *, Error **),
                                   void (*set)(Object *, const char *,
                                               Error **));

/**
 * object_property_add_bool:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @get: the getter or NULL if the property is write-only.
 * @set: the setter or NULL if the property is read-only
 *
 * Add a bool property using getters/setters.  This function will add a
 * property of type 'bool'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_bool(Object *obj, const char *name,
                              bool (*get)(Object *, Error **),
                              void (*set)(Object *, bool, Error **));

ObjectProperty *object_class_property_add_bool(ObjectClass *klass,
                                    const char *name,
                                    bool (*get)(Object *, Error **),
                                    void (*set)(Object *, bool, Error **));

/**
 * object_property_add_enum:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @typename: the name of the enum data type
 * @get: the getter or %NULL if the property is write-only.
 * @set: the setter or %NULL if the property is read-only
 *
 * Add an enum property using getters/setters.  This function will add a
 * property of type '@typename'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_enum(Object *obj, const char *name,
                              const char *typename,
                              const QEnumLookup *lookup,
                              int (*get)(Object *, Error **),
                              void (*set)(Object *, int, Error **));

ObjectProperty *object_class_property_add_enum(ObjectClass *klass,
                                    const char *name,
                                    const char *typename,
                                    const QEnumLookup *lookup,
                                    int (*get)(Object *, Error **),
                                    void (*set)(Object *, int, Error **));

/**
 * object_property_add_tm:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @get: the getter or NULL if the property is write-only.
 *
 * Add a read-only struct tm valued property using a getter function.
 * This function will add a property of type 'struct tm'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_tm(Object *obj, const char *name,
                            void (*get)(Object *, struct tm *, Error **));

ObjectProperty *object_class_property_add_tm(ObjectClass *klass,
                            const char *name,
                            void (*get)(Object *, struct tm *, Error **));

typedef enum {
    /* Automatically add a getter to the property */
    OBJ_PROP_FLAG_READ = 1 << 0,
    /* Automatically add a setter to the property */
    OBJ_PROP_FLAG_WRITE = 1 << 1,
    /* Automatically add a getter and a setter to the property */
    OBJ_PROP_FLAG_READWRITE = (OBJ_PROP_FLAG_READ | OBJ_PROP_FLAG_WRITE),
} ObjectPropertyFlags;

/**
 * object_property_add_uint8_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @flags: bitwise-or'd ObjectPropertyFlags
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint8'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_uint8_ptr(Object *obj, const char *name,
                                              const uint8_t *v,
                                              ObjectPropertyFlags flags);

ObjectProperty *object_class_property_add_uint8_ptr(ObjectClass *klass,
                                         const char *name,
                                         const uint8_t *v,
                                         ObjectPropertyFlags flags);

/**
 * object_property_add_uint16_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @flags: bitwise-or'd ObjectPropertyFlags
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint16'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_uint16_ptr(Object *obj, const char *name,
                                    const uint16_t *v,
                                    ObjectPropertyFlags flags);

ObjectProperty *object_class_property_add_uint16_ptr(ObjectClass *klass,
                                          const char *name,
                                          const uint16_t *v,
                                          ObjectPropertyFlags flags);

/**
 * object_property_add_uint32_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @flags: bitwise-or'd ObjectPropertyFlags
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint32'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_uint32_ptr(Object *obj, const char *name,
                                    const uint32_t *v,
                                    ObjectPropertyFlags flags);

ObjectProperty *object_class_property_add_uint32_ptr(ObjectClass *klass,
                                          const char *name,
                                          const uint32_t *v,
                                          ObjectPropertyFlags flags);

/**
 * object_property_add_uint64_ptr:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @v: pointer to value
 * @flags: bitwise-or'd ObjectPropertyFlags
 *
 * Add an integer property in memory.  This function will add a
 * property of type 'uint64'.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_uint64_ptr(Object *obj, const char *name,
                                    const uint64_t *v,
                                    ObjectPropertyFlags flags);

ObjectProperty *object_class_property_add_uint64_ptr(ObjectClass *klass,
                                          const char *name,
                                          const uint64_t *v,
                                          ObjectPropertyFlags flags);

/**
 * object_property_add_alias:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @target_obj: the object to forward property access to
 * @target_name: the name of the property on the forwarded object
 *
 * Add an alias for a property on an object.  This function will add a property
 * of the same type as the forwarded property.
 *
 * The caller must ensure that <code>@target_obj</code> stays alive as long as
 * this property exists.  In the case of a child object or an alias on the same
 * object this will be the case.  For aliases to other objects the caller is
 * responsible for taking a reference.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_alias(Object *obj, const char *name,
                               Object *target_obj, const char *target_name);

/**
 * object_property_add_const_link:
 * @obj: the object to add a property to
 * @name: the name of the property
 * @target: the object to be referred by the link
 *
 * Add an unmodifiable link for a property on an object.  This function will
 * add a property of type link<TYPE> where TYPE is the type of @target.
 *
 * The caller must ensure that @target stays alive as long as
 * this property exists.  In the case @target is a child of @obj,
 * this will be the case.  Otherwise, the caller is responsible for
 * taking a reference.
 *
 * Returns: The newly added property on success, or %NULL on failure.
 */
ObjectProperty *object_property_add_const_link(Object *obj, const char *name,
                                               Object *target);

/**
 * object_property_set_description:
 * @obj: the object owning the property
 * @name: the name of the property
 * @description: the description of the property on the object
 *
 * Set an object property's description.
 *
 * Returns: %true on success, %false on failure.
 */
void object_property_set_description(Object *obj, const char *name,
                                     const char *description);
void object_class_property_set_description(ObjectClass *klass, const char *name,
                                           const char *description);

/**
 * object_child_foreach:
 * @obj: the object whose children will be navigated
 * @fn: the iterator function to be called
 * @opaque: an opaque value that will be passed to the iterator
 *
 * Call @fn passing each child of @obj and @opaque to it, until @fn returns
 * non-zero.
 *
 * It is forbidden to add or remove children from @obj from the @fn
 * callback.
 *
 * Returns: The last value returned by @fn, or 0 if there is no child.
 */
int object_child_foreach(Object *obj, int (*fn)(Object *child, void *opaque),
                         void *opaque);

/**
 * object_child_foreach_recursive:
 * @obj: the object whose children will be navigated
 * @fn: the iterator function to be called
 * @opaque: an opaque value that will be passed to the iterator
 *
 * Call @fn passing each child of @obj and @opaque to it, until @fn returns
 * non-zero. Calls recursively, all child nodes of @obj will also be passed
 * all the way down to the leaf nodes of the tree. Depth first ordering.
 *
 * It is forbidden to add or remove children from @obj (or its
 * child nodes) from the @fn callback.
 *
 * Returns: The last value returned by @fn, or 0 if there is no child.
 */
int object_child_foreach_recursive(Object *obj,
                                   int (*fn)(Object *child, void *opaque),
                                   void *opaque);
/**
 * container_get:
 * @root: root of the #path, e.g., object_get_root()
 * @path: path to the container
 *
 * Return a container object whose path is @path.  Create more containers
 * along the path if necessary.
 *
 * Returns: the container object.
 */
Object *container_get(Object *root, const char *path);

/**
 * object_type_get_instance_size:
 * @typename: Name of the Type whose instance_size is required
 *
 * Returns the instance_size of the given @typename.
 */
size_t object_type_get_instance_size(const char *typename);

/**
 * object_property_help:
 * @name: the name of the property
 * @type: the type of the property
 * @defval: the default value
 * @description: description of the property
 *
 * Returns: a user-friendly formatted string describing the property
 * for help purposes.
 */
char *object_property_help(const char *name, const char *type,
                           QObject *defval, const char *description);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(Object, object_unref)

#endif