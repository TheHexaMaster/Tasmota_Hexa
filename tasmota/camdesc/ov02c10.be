# in preinit.be: load("/ov02c10.be")
class CSI_Sensor
  var name
  var wire
  var addr
  
  static REG_END = 0xFFFF
  static REG_DELAY = 0xFFFE
  
  def init(name, addr)
    self.name = name
    self.addr = addr
  end
  
  def write_reg16(reg, val)
    if !self.wire return false end
    self.wire._begin_transmission(self.addr)
    self.wire._write((reg >> 8) & 0xFF)
    self.wire._write(reg & 0xFF)
    self.wire._write(val)
    return self.wire._end_transmission() == 0
  end
  
  def read_reg16(reg)
    if !self.wire return nil end
    self.wire._begin_transmission(self.addr)
    self.wire._write((reg >> 8) & 0xFF)
    self.wire._write(reg & 0xFF)
    var err = self.wire._end_transmission(false)
    if err != 0 return nil end
    self.wire._request_from(self.addr, 1)
    if self.wire._available()
      return self.wire._read()
    end
    return nil
  end
  
  def write_array(reg_array)
    var i = 0
    var sz = size(reg_array)
    while i < sz
      var reg = reg_array[i][0]
      var val = reg_array[i][1]
      if reg == self.REG_END
        break
      elif reg == self.REG_DELAY
        tasmota.delay(val)
      else
        if !self.write_reg16(reg, val)
          print(format("%s: FAILED at reg 0x%04X", self.name, reg))
          return false
        end
      end
      i += 1
    end
    return true
  end
end

class OV02C10 : CSI_Sensor
  static ADDR = 0x36
  static CHIP_ID = 0x5602
  var is_streaming
  var is_initialized
  var width, height
  var mipi_clock
  var format, bin_mode
  
  def init()
    super(self).init("OV02C10", self.ADDR)
    self.is_streaming = false
    self.is_initialized = false
    self.width = 1920
    self.height = 1080
    self.mipi_clock = 800
    self.format = 1
    self.bin_mode = 1
  end
  
  def detect()
    self.wire = tasmota.wire_scan(self.addr)
    if !self.wire
      print("OV02C10: I2C scan failed")
      return false
    end
    tasmota.delay(10)
    var id_h = self.read_reg16(0x300A)
    var id_l = self.read_reg16(0x300B)
    if id_h == nil || id_l == nil return false end
    var chip_id = (id_h << 8) | id_l
    print(format("OV02C10: Chip ID = 0x%04X", chip_id))
    return chip_id == self.CHIP_ID
  end
  
  def stream_on(on)
    if !self.wire return false end
    if on
      if self.is_streaming return true end
      if !self.write_reg16(0x0100, 0x01) return false end
      self.is_streaming = true
      print("OV02C10: Stream ON")
    else
      if !self.write_reg16(0x0100, 0x00) return false end
      self.is_streaming = false
      print("OV02C10: Stream OFF")
    end
    return true
  end

  def common_regs()
    return [
      [0x0103, 0x01],
      [self.REG_DELAY, 10],
      [0x0100, 0x00],
      [self.REG_END, 0x00]
    ]
  end

  # Full baseline register init (2-lane) + dynamic sensor window/VTS
  # Matches Espressif esp-video-components PR #46 for OV02C10
  def regs_custom(x, y, w, h, bin, fps, fmt)
    # Sensor constraints: no binning, RAW10 only
    bin = 1
    fmt = 1
    w = (w / 8) * 8
    if w < 64  w = 64   end
    if w > 1920 w = 1920 end
    if h < 64  h = 64   end
    if h > 1080 h = 1080 end
    if fps < 1  fps = 1  end
    if fps > 60 fps = 60 end

    self.width = w
    self.height = h
    self.format = fmt
    self.bin_mode = bin
    self.mipi_clock = 800

    # HTS=2280 per Espressif PR (0x08E8)
    var hts = 2280
    # VTS: FPS = 80000000 / (HTS * VTS)
    var vts = 80000000 / (hts * fps)
    if vts < 1164  vts = 1164  end
    if vts > 65535 vts = 65535 end

    # Dynamic sensor window (0x3800-0x3807): center crop from 1928x1092
    # Full array: x_start=0x0000, y_start=0x0004, x_end=0x078F, y_end=0x0443
    var full_w = 1928
    var full_h = 1088
    var win_x_start = (full_w - w) / 2 + x
    var win_y_start = (full_h - h) / 2 + 4 + y
    if win_x_start < 0 win_x_start = 0 end
    if win_y_start < 4 win_y_start = 4 end
    var win_x_end = win_x_start + w + 7
    var win_y_end = win_y_start + h + 3
    if win_x_end > 0x078F win_x_end = 0x078F end
    if win_y_end > 0x0443 win_y_end = 0x0443 end

    # ISP offsets: fixed values per Espressif PR
    var isp_off_x = 0x07
    var isp_off_y = 0x04

    # Exposure: near-maximum for good indoor brightness
    # AEC/AGC (0x3503=0x00) will auto-adjust from this starting point
    var exposure = vts - 14
    if exposure < 4 exposure = 4 end

    print(format("OV02C10: CFG %dx%d FPS=%d VTS=%d HTS=%d MIPI=%dMbps", w, h, fps, vts, hts, self.mipi_clock))

    return [
      # --- PLL & System ---
      [0x0301, 0x08], [0x0303, 0x06], [0x0304, 0x01], [0x0305, 0x90],
      [0x0313, 0x40], [0x031C, 0x4F],
      [0x301B, 0xF0], [0x3020, 0x97], [0x3022, 0x01], [0x3026, 0xB4],
      [0x3027, 0xF1], [0x303B, 0x00], [0x303C, 0x4F], [0x303D, 0xE6],
      [0x303E, 0x00], [0x303F, 0x03], [0x3021, 0x23],

      # --- Exposure & Gain (dynamic) ---
      [0x3501, (exposure >> 8) & 0xFF], [0x3502, exposure & 0xFF], [0x3504, 0x0C],
      [0x3507, 0x00], [0x3508, 0x40], [0x3509, 0x00],
      [0x350A, 0x01], [0x350B, 0x00], [0x350C, 0x41],

      # --- Analog ---
      [0x3600, 0x84], [0x3603, 0x08], [0x3610, 0x57], [0x3611, 0x1B],
      [0x3613, 0x78], [0x3623, 0x00], [0x3632, 0xA0], [0x3642, 0xE8],
      [0x364C, 0x70], [0x365F, 0x0F],

      # --- Sensor Core ---
      [0x3708, 0x30], [0x3714, 0x24], [0x3725, 0x02], [0x3737, 0x08],
      [0x3739, 0x28], [0x3749, 0x32], [0x374A, 0x32], [0x374B, 0x32],
      [0x374C, 0x32], [0x374D, 0x81], [0x374E, 0x81], [0x374F, 0x81],
      [0x3752, 0x36], [0x3753, 0x36], [0x3754, 0x36], [0x3761, 0x00],
      [0x376C, 0x81], [0x3774, 0x18], [0x3776, 0x08], [0x377C, 0x81],
      [0x377D, 0x81], [0x377E, 0x81], [0x37A0, 0x44], [0x37A6, 0x44],
      [0x37AA, 0x0D], [0x37AE, 0x00], [0x37CB, 0x03], [0x37CC, 0x01],
      [0x37D8, 0x02], [0x37D9, 0x10], [0x37E1, 0x10], [0x37E2, 0x18],
      [0x37E3, 0x08], [0x37E4, 0x08], [0x37E5, 0x02], [0x37E6, 0x08],

      # --- Geometry (dynamic: sensor window, output size, VTS) ---
      [0x3800, (win_x_start >> 8) & 0xFF], [0x3801, win_x_start & 0xFF],
      [0x3802, (win_y_start >> 8) & 0xFF], [0x3803, win_y_start & 0xFF],
      [0x3804, (win_x_end >> 8) & 0xFF],   [0x3805, win_x_end & 0xFF],
      [0x3806, (win_y_end >> 8) & 0xFF],   [0x3807, win_y_end & 0xFF],
      [0x3808, (w >> 8) & 0xFF], [0x3809, w & 0xFF],
      [0x380A, (h >> 8) & 0xFF], [0x380B, h & 0xFF],
      [0x380C, (hts >> 8) & 0xFF], [0x380D, hts & 0xFF],
      [0x380E, (vts >> 8) & 0xFF], [0x380F, vts & 0xFF],
      [0x3810, 0x00], [0x3811, isp_off_x],
      [0x3812, 0x00], [0x3813, isp_off_y],
      [0x3814, 0x01], [0x3815, 0x01], [0x3816, 0x01], [0x3817, 0x01],

      # --- ISP / Readout ---
      [0x3820, 0xA0], [0x3821, 0x00], [0x3822, 0x80], [0x3823, 0x08],
      [0x3824, 0x00], [0x3825, 0x20], [0x3826, 0x00], [0x3827, 0x08],
      [0x382A, 0x00], [0x382B, 0x08], [0x382D, 0x00], [0x382E, 0x00],
      [0x382F, 0x23], [0x3834, 0x00], [0x3839, 0x00], [0x383A, 0xD1],
      [0x383E, 0x03],

      # --- Phase / Timing ---
      [0x393D, 0x29], [0x393F, 0x6E],
      [0x394B, 0x06], [0x394C, 0x06], [0x394D, 0x08], [0x394E, 0x0A],
      [0x394F, 0x01], [0x3950, 0x01], [0x3951, 0x01], [0x3952, 0x01],
      [0x3953, 0x01], [0x3954, 0x01], [0x3955, 0x01], [0x3956, 0x01],
      [0x3957, 0x0E], [0x3958, 0x08], [0x3959, 0x08], [0x395A, 0x08],
      [0x395B, 0x13], [0x395C, 0x09], [0x395D, 0x05], [0x395E, 0x02],
      [0x395F, 0x00], [0x3960, 0x00], [0x3961, 0x00], [0x3962, 0x00],
      [0x3963, 0x00], [0x3964, 0x00], [0x3965, 0x00], [0x3966, 0x00],
      [0x3967, 0x00], [0x3968, 0x01], [0x3969, 0x01], [0x396A, 0x01],
      [0x396B, 0x01], [0x396C, 0x10], [0x396D, 0xF0], [0x396E, 0x11],
      [0x396F, 0x00], [0x3970, 0x37], [0x3971, 0x37], [0x3972, 0x37],
      [0x3973, 0x37], [0x3974, 0x00], [0x3975, 0x3C], [0x3976, 0x3C],
      [0x3977, 0x3C], [0x3978, 0x3C],

      # --- Clock / Debug ---
      [0x3C00, 0x0F], [0x3C20, 0x01], [0x3C21, 0x08],
      [0x3F00, 0x8B], [0x3F02, 0x0F],

      # --- BLC / ISP Control ---
      [0x4000, 0xC3], [0x4001, 0xE0], [0x4002, 0x00], [0x4003, 0x40],
      [0x4008, 0x04], [0x4009, 0x23], [0x400A, 0x04], [0x400B, 0x01],
      [0x4041, 0x20],
      [0x4077, 0x06], [0x4078, 0x00], [0x4079, 0x1A], [0x407A, 0x7F],
      [0x407B, 0x01], [0x4080, 0x03], [0x4081, 0x84],

      # --- Format ---
      [0x4308, 0x03], [0x4309, 0xFF], [0x430D, 0x00],

      # --- MIPI ---
      [0x4806, 0x00], [0x4813, 0x00], [0x4837, 0x10], [0x4857, 0x05],
      [0x4884, 0x04],
      [0x4500, 0x07], [0x4501, 0x00], [0x4503, 0x00],
      [0x450A, 0x04], [0x450E, 0x00], [0x450F, 0x00],
      [0x4800, 0x64], [0x4900, 0x00], [0x4901, 0x00], [0x4902, 0x01],

      # --- OTP / Misc ---
      [0x4D00, 0x03], [0x4D01, 0xD8], [0x4D02, 0xBA], [0x4D03, 0xA0],
      [0x4D04, 0xB7], [0x4D05, 0x34], [0x4D0D, 0x00],

      # --- ISP Pipeline ---
      [0x5000, 0xFD], [0x5001, 0x50], [0x5006, 0x00], [0x5080, 0x40],
      [0x5181, 0x2B], [0x5202, 0xA3], [0x5206, 0x01], [0x5207, 0x00],
      [0x520A, 0x01], [0x520B, 0x00],

      # --- 2-Lane Tail ---
      [0x365D, 0x00], [0x4815, 0x40], [0x4816, 0x12], [0x481F, 0x30],
      [0x4F00, 0x01], [0x0316, 0x90], [0x3016, 0x32],

      # Enable AEC (auto exposure) and AGC (auto gain)
      # Linux driver uses manual mode (V4L2 controls it); Tasmota needs on-sensor auto
      [0x3503, 0x00],

      [self.REG_END, 0x00]
    ]
  end

  def camera(cmd, idx, payload, raw)
    if cmd == "init"
      print("OV02C10: ========== INIT ==========")
      if !self.is_initialized
        if !self.detect() return 0 end
        self.is_initialized = true
      end

      if !self.write_array(self.common_regs()) return 0 end

      var req_w=1920; var req_h=1080; var req_bin=1; var req_fps=30; var req_fmt=1
      var req_x=0; var req_y=0
      var res_idx = 0

      if idx != 0
        import introspect
        var p = introspect.toptr(idx)
        var b = bytes(p, 28)
        res_idx = b[26]

        if res_idx == 255
          req_w = b.get(0, 2); req_h = b.get(2, 2)
          req_fmt = b[8]; req_bin = b[16]; req_fps = b[17]
          req_x = b.get(12, 2); req_y = b.get(14, 2)
        elif res_idx == 1
          req_w=640; req_h=480; req_bin=1; req_fmt=1
        elif res_idx == 0
          req_w=1280; req_h=720; req_bin=1; req_fmt=1
        elif res_idx == 2
          req_w=1920; req_h=1080; req_bin=1; req_fmt=1
        end
      end

      var regs = self.regs_custom(req_x, req_y, req_w, req_h, req_bin, req_fps, req_fmt)
      if !self.write_array(regs) return 0 end

      if idx != 0
        import introspect
        var p = introspect.toptr(idx)
        var b = bytes(p, 28)
        b.set(0, self.width, 2)
        b.set(2, self.height, 2)
        b.set(4, 1920, 2)
        b.set(6, 1080, 2)
        b[8] = self.format
        b[9] = 2
        b.set(10, self.mipi_clock, 2)
        b[16] = self.bin_mode
        b[17] = req_fps
        b.setbytes(18, bytes().fromstring("OV02C10"))
      end

      return 1

    elif cmd == "stream"
      return self.stream_on(idx == 1) ? 1 : 0
    end
    return 0
  end
end

var ov02c10 = OV02C10()
tasmota.add_driver(ov02c10)
# wcwindow 0,0,1920,1080,1,30,1
# wcwindow 0,0,1280,720,1,30,1