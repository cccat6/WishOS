local C=notes.require('../suite/common.lua');local pending=false
function on_start()
 C.init();C.open('device',{status=C.t('正在初始化传感器','Initializing sensor')});local r,e=C.call('/dev/imu','configure',{})
 if not r then C.update({status=e or ''});return end
 notes.on('/dev/imu','completed',function(e)
  pending=false;local d=e.data;C.update({accel=string.format('X %+.2f   Y %+.2f   Z %+.2f',d.accel_x_g,d.accel_y_g,d.accel_z_g),gyro=string.format('X %+.1f   Y %+.1f   Z %+.1f',d.gyro_x_dps,d.gyro_y_dps,d.gyro_z_dps),temperature=string.format(C.t('芯片温度 %.1f °C','Die temperature %.1f °C'),d.die_temperature_c),status=C.t('倾斜或转动设备，观察数值变化','Tilt or rotate the device')})
 end)
 notes.on('/dev/imu','failed',function(e) pending=false;C.update({status=e.data.error or ''}) end)
 C.timer(250,C.config.imu_interval_ms,function() if not pending then pending=C.call('/dev/imu','measure')~=nil end end)
 C.button(function() C.launch('voice',{record=true}) end)
end
