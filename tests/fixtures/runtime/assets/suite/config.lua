return {
  buttons={previous='A',select='B',next='C'},
  consume_buttons=true,
  status_interval_ms=60000,
  sleep_timer_ms=900000,
  auto_rotate=false,
  -- A host application may register this route on the ordinary remote bridge.
  voice_enabled=true,
  voice_timeout_ms=900000,
  show_recording=false,
  show_processing=true,
  recording={max_ms=300000},
  pages={
    apps={driver='e6.blue-yellow.v1',layout='menu'},
    welcome={driver='e6.color-fast.v1'},
    home={driver='e6.color-fast.v1'},
    menu={driver='e6.blue-yellow.v1'},
    network={driver='e6.blue-yellow.v1'},
    services={driver='e6.color-fast.v1'},
    storage={driver='e6.blue-yellow.v1'},
    status={driver='e6.color-fast.v1'},
    recording={driver='e6.blue-yellow.v1'},
    sleep={driver='e6.blue-yellow.v1'},
  },
  menu={
    {label='网络',label_en='Network',page='network'},
    {label='服务',label_en='Services',page='services'},
    {label='应用',label_en='Apps',page='apps'},
    {label='设备状态',label_en='Device status',page='status'},
    {label='存储',label_en='Storage',page='storage'},
    {label='休眠',label_en='Sleep',page='sleep'}
  }
}
