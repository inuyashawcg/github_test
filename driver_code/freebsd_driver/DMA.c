/*
    简单介绍DMA的基本特性，应用场景是一个需要使用DMA的虚设备的device_attach例程
*/

static int
foo_attach(device_t dev)
{
    struct foo_softc *sc = device_get_softc(dev);
    int error;

    bzero(sc, sizeof(*sc));

    /* 创建了一个名为 foo_parent_dma_tag 的DMA tag*/
    if(bus_dma_tag_create(bus_get_dma_tag(dev),
                        1,
                        0,
                        BUS_SPACE_MAXADDR,
                        BUS_SPACE_MAXADDR,
                        NULL,
                        NULL,
                        BUS_SPACE_MAXSIZE_32BIT,
                        BUS_SPACE_UNRESTRICTED,
                        BUS_SPACE_MAXSIZE_32BIT,
                        0,
                        NULL,
                        NULL,
                        &sc->foo_parent_dma_tag)) {
        device_printf(dev, "Cannot allocate parent DMA tag!\n");
        return (ENOMEM);
    }

    /* 
        再次调用create函数，不过要注意第一次传入的参数是 sc->foo_parent_dma_tag，这里涉及到了
        到了DMA类似于继承的性质。一个DMA标签可以从其他的DMA标签处继承特性和限制，并且子标签不允许
        放宽由其父标签所建立的限制。所以，这里的 foo_baz_dma_tag 其实就是 foo_parent_dma_tag
        的严苛版本
    */
    if(bus_dma_tag_create(sc->foo_parent_dma_tag,
                        1,
                        0,
                        BUS_SPACE_MAXADDR,
                        BUS_SPACE_MAXADDR,
                        NULL,
                        NULL,
                        BUS_SPACE_MAXSIZE_32BIT,
                        BUS_SPACE_UNRESTRICTED,
                        BUS_SPACE_MAXSIZE_32BIT,
                        0,
                        NULL,
                        NULL,
                        &sc->foo_baz_dma_tag)) {
        device_printf(dev, "Cannot allocate baz DMA tag!\n");
        return (ENOMEM);
    }

    /*
        bus_dmamap_create 创建了一个名为 foo_baz_dma_map 的DMA映射，粗略地说，DMA映射用于表示依据
        DMA标签属性而分配的内存区域，隶属于设备可见的地址空间
    */
    if (bus_dmamap_create(sc->foo_baz_dma_tag,
                            0,
                            &sc->foo_baz_dma_map)) {
        device_printf(dev, "Cannot allocate baz DMA map!\n");
        return (ENOMEM);
    }

    bzero(sc->foo_baz_buf, BAZ_BUF_SIZE);

    /*
        最后，通过 bus_dmamap_load 函数将 foo_baz_buf 加载到与DMA映射 foo_baz_dma_map 相关联的设备
        可见地址。DMA可以使用任意的缓冲区，但是在缓冲区映射到设备可见地址(也就是一个DMA映射)之前，设备是不能
        对它们进行访问的
        还需要注意，load里边其实包含了一个回调函数，foo_callback
    */
    error = bus_dmamap_load(sc->foo_baz_dma_tag,
                            sc->foo_baz_dma_map,
                            sc->foo_baz_buf,
                            BAZ_BUF_SIZE,
                            foo_callback,
                            &sc->foo_baz_busaddr,
                            BUS_DMA_NOWAIT);

    if (error || sc->foo_baz_busaddr == 0) {
        device_printf(dev, "Cannot map baz DMA memory!\n");
        return (ENOMEM);
    }
    ...
}

/*
    bus_dma_segment_t 结构体变量包含了两个元素，一个就是DMA的地址，另一个是传输的数据长度
    该函数会在缓冲区加载完成后调用，如果成功，缓冲区的载入地址(segs[0].ds_addr)会通过参数arg
    返回。否则，函数foo_callback什么也不做
*/
static void
foo_callback(void *arg, bus_dma_segment_t *segs, int nesg, int error)
{
    if (error)
        return;
    *(bus_addr_t*)arg = segs[0].ds_addr;
}

/*
    在缓冲区成功加载之后，我们就可以传输数据了。大多数设备只需要把缓冲区的设备可见地址写入到一个专用的
    寄存器就可以启动一个DMA数据传输了，示例函数如下。

    该函数的功能就是将设备可见地址写入到寄存器当中
    sc -> foo_baz_busaddr: 缓冲区的设备可见地址，callback函数会将地址返回到这里
    FOO_BAZ: 寄存器
*/
bus_write_4(sc -> foo_io_resource, FOO_BAZ, sc -> foo_baz_busaddr);


/* 取消DMA,注意执行的顺序*/
static int
foo_detach(device_t dev)
{
    struct foo_softc *sc = device_get_softc(dev);

    if (sc -> foo_baz_busaddr != 0)
        bus_dmamap_unload(sc->foo_baz_dma_tag, sc->foo_baz_dma_map);
    
    if (sc -> foo_baz_dma_map != NULL)
        bus_dmamap_destroy(sc->foo_baz_dma_tag, sc->foo_baz_dma_map);

    if (sc -> foo_baz_dma_tag != NULL)
        bus_dma_tag_destroy(sc->foo_baz_dma_tag);
    
    if (sc -> foo_parent_dma_tag != NULL)
        bus_dma_tag_destroy(sc->foo_parent_dma_tag);
    ...
}