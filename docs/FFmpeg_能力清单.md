# FFmpeg 能力清单

> **库**：`vendor/ffmpeg`（来源 `ffmpeg-gpl-full`，FFmpeg `9.0.2-gplfull`，GPL v3）  
> **依据**：构建产物自带的两份权威文件，**未运行任何编解码测试**

| 依据文件                                                        | 提供内容                                                                          |
| ----------------------------------------------------------- | ----------------------------------------------------------------------------- |
| `ffmpeg-gpl-full/evidence/build-logs/02-configure-argv.txt` | 实际 configure 参数（含 `--disable-encoder=` 共 100 项黑名单）                            |
| `ffmpeg-gpl-full/evidence/build-logs/02-configure.log`      | `Enabled decoders / encoders / demuxers / muxers / hwaccels / protocols` 权威清单 |

---

## 1. 总览

configure 里**没有** `--disable-decoder`、也**没有** `--disable-everything`  
—— 所以解码器是 FFmpeg 9.0 的**完整默认集**；  
而 `--disable-encoder=` 砍掉了 100 项。**简言之：解码几乎全覆盖，编码有明确禁区。**

| 类别     |      解码 |      编码 |
| ------ | ------: | ------: |
| 视频     | **296** |  **58** |
| 音频     | **217** |  **51** |
| 字幕     |  **23** |  **11** |
| **合计** | **536** | **120** |

| 其他组件               |  数量 |
| ------------------ | --: |
| 解封装器 demuxer       | 359 |
| 封装器 muxer          | 184 |
| 硬件解码 hwaccel       |  51 |
| 协议 protocol        |  36 |
| 解析器 parser         |  68 |
| 滤镜 filter（**未加载**） | 502 |

> 滤镜虽然编译进去了，但本项目**未加载** `avfilter-12.dll`（缩放走 swscale），>   
> 详见 `vendor/ffmpeg/README.md`。

---

## 2. 视频解码 — 296 项，全部可用

### 现代主流与硬件加速（30 项）

`apv` `av1` `av1_amf` `av1_cuvid` `av1_qsv` `h264`  
`h264_amf` `h264_cuvid` `h264_qsv` `hevc` `hevc_amf` `hevc_cuvid`  
`hevc_qsv` `libaom_av1` `libvpx_vp8` `libvpx_vp9` `mpeg1_cuvid` `mpeg2_cuvid`  
`mpeg2_qsv` `mpeg4_cuvid` `mpegvideo` `vp8` `vp8_cuvid` `vp8_qsv`  
`vp9` `vp9_amf` `vp9_cuvid` `vp9_qsv` `vvc` `vvc_qsv`

### MPEG 家族（12 项）

`cavs` `dirac` `h261` `h263` `h263i` `h263p`  
`mpeg1video` `mpeg2video` `mpeg4` `msmpeg4v1` `msmpeg4v2` `msmpeg4v3`

### 微软 / Real / 苹果（23 项）

`cinepak` `indeo2` `indeo3` `indeo4` `indeo5` `msa1`  
`mss1` `mss2` `rv10` `rv20` `rv30` `rv40`  
`rv60` `svq1` `svq3` `vc1` `vc1_cuvid` `vc1_qsv`  
`vc1image` `wmv1` `wmv2` `wmv3` `wmv3image`

### 早期开源 / 广播（10 项）

`dvvideo` `snow` `theora` `vp3` `vp4` `vp5`  
`vp6` `vp6a` `vp6f` `vp7`

### 专业 / 中间片 / 无损（20 项）

`cfhd` `cllc` `dds` `dnxhd` `dxv` `ffv1`  
`ffvhuff` `hap` `hq_hqa` `hqx` `huffyuv` `lagarith`  
`magicyuv` `notchlc` `prores` `prores_raw` `sheervideo` `utvideo`  
`zero12v` `zerocodec`

### 图像 / 图像序列（57 项）

`alias_pix` `apng` `avrp` `avui` `bitpacked` `bmp`  
`brender_pix` `dpx` `exr` `fic` `fits` `gif`  
`jpeg2000` `jpegls` `mjpeg` `mjpeg_cuvid` `mjpeg_qsv` `mjpegb`  
`mxpeg` `pam` `pbm` `pcx` `pfm` `pgm`  
`pgmyuv` `pgx` `phm` `photocd` `pictor` `png`  
`ppm` `psd` `ptx` `qdraw` `qoi` `r10k`  
`r210` `rawvideo` `sgi` `smvjpeg` `sp5x` `sunrast`  
`targa` `targa_y216` `tiff` `v210` `v210x` `wbmp`  
`webp` `webp_anim` `wrapped_avframe` `xbm` `xface` `xl`  
`xpm` `xwd` `yuv4`

### 屏幕录制 / 游戏 / 专用格式（144 项）

`aasc` `acelp_kelvin` `agm` `ahx` `aic` `amv`  
`anm` `ansi` `apac` `arbc` `argo` `asv1`  
`asv2` `aura` `aura2` `avrn` `avs` `bethsoftvid`  
`bfi` `bink` `bintext` `bmv_video` `c93` `cdgraphics`  
`cdtoons` `cdxl` `clearvideo` `cljr` `cpia` `cri`  
`cscd` `cyuv` `dfa` `dsicinvideo` `dxa` `dxtory`  
`eacmv` `eamad` `eatgq` `eatgv` `eatqi` `eightbps`  
`escape124` `escape130` `flashsv` `flashsv2` `flic` `flv`  
`fmvc` `fourxm` `fraps` `frwu` `g2m` `gdv`  
`gem` `hdr` `hnm4_video` `hymt` `idcin` `iff_ilbm`  
`imm4` `imm5` `interplay_video` `ipu` `jv` `kgv1`  
`kmvc` `lead` `loco` `lscr` `m101` `mdec`  
`media100` `mimic` `misc4` `mmvideo` `mobiclip` `motionpixels`  
`mscc` `msp2` `msrle` `msvideo1` `mszh` `mts2`  
`mv30` `mvc1` `mvc2` `mvdv` `mvha` `mwsc`  
`nuv` `paf_video` `pdv` `pixlet` `prosumer` `qpeg`  
`qtrle` `rasc` `rl2` `roq` `rpza` `rscc`  
`rtv1` `sanm` `scpr` `screenpresso` `sga` `sgirle`  
`simbiosis_imx` `smacker` `smc` `speedhq` `srgc` `tdsc`  
`thp` `tiertexseqvideo` `tmv` `truemotion1` `truemotion2` `truemotion2rt`  
`tscc` `tscc2` `txd` `ulti` `vb` `vble`  
`vbn` `vcr1` `vmdvideo` `vmix` `vmnc` `vnull`  
`vqa` `vqc` `wcmv` `wnv1` `xan_wc3` `xan_wc4`  
`xbin` `y41p` `ylc` `yop` `zlib` `zmbv`

---

## 3. 视频编码 — 58 项可用

| 分组           | 数量 | 编码器                                                                                                      |
| ------------ | -: | -------------------------------------------------------------------------------------------------------- |
| 现代格式（软件）     |  7 | `libx264` `libx264rgb` `libx265` `libaom_av1` `libsvtav1` `libvpx_vp8` `libvpx_vp9`                      |
| H.264 硬件     |  6 | `h264_nvenc` `h264_qsv` `h264_amf` `h264_vaapi` `h264_d3d12va` `h264_mf`                                 |
| HEVC 硬件      |  6 | `hevc_nvenc` `hevc_qsv` `hevc_amf` `hevc_vaapi` `hevc_d3d12va` `hevc_mf`                                 |
| AV1 硬件       |  5 | `av1_nvenc` `av1_qsv` `av1_amf` `av1_vaapi` `av1_mf`                                                     |
| VP8 / VP9 硬件 |  3 | `vp9_qsv` `vp9_vaapi` `vp8_vaapi`                                                                        |
| MPEG-2 硬件    |  2 | `mpeg2_qsv` `mpeg2_vaapi`                                                                                |
| 专业 / 无损      |  7 | `prores` `prores_aw` `prores_ks` `dnxhd` `ffv1` `ffvhuff` `huffyuv`                                      |
| 图像 / 静态      | 12 | `mjpeg` `mjpeg_qsv` `mjpeg_vaapi` `png` `gif` `bmp` `libwebp` `libwebp_anim` `zlib` `pfm` `pgmyuv` `pdv` |
| 原始 / 其他      | 10 | `rawvideo` `wrapped_avframe` `bitpacked` `avui` `alias_pix` `amv` `dxv` `qtrle` `roq` `vnull`            |

### 被 `--disable-encoder` 禁用的 100 项

**① 视频类 38 项** —— 老式 / 早期格式，**只能解、不能编**

`a64multi` `a64multi5` `asv1` `asv2` `cfhd` `cinepak`  
`dvvideo` `flashsv` `flashsv2` `flv` `h261` `h263`  
`h263p` `libtheora` `libxvid` `magicyuv` `mpeg1video` `mpeg2video`  
`mpeg4` `msmpeg4` `msmpeg4v2` `msrle` `msvideo1` `roqvideo`  
`rpza` `rv10` `rv20` `smc` `snow` `speedhq`  
`svq1` `utvideo` `vbn` `vc2` `wmv1` `wmv2`  
`y41p` `zmbv`

**② 图像类 29 项** —— 专业图像格式，**只能解、不能编**

`apng` `avrp` `cljr` `dpx` `exr` `fits`  
`hdr` `jpeg2000` `jpegls` `ljpeg` `pam` `pbm`  
`pcx` `pgm` `phm` `ppm` `qoi` `r10k`  
`r210` `sgi` `sunrast` `targa` `tiff` `v210`  
`wbmp` `xbm` `xface` `xwd` `yuv4`

**③ 音频类 33 项** —— 老旧 / 冷门音频编码

`adpcm_adx` `adpcm_argo` `adpcm_g722` `adpcm_g726` `adpcm_g726le` `adpcm_ima_alp`  
`adpcm_ima_amv` `adpcm_ima_apm` `adpcm_ima_qt` `adpcm_ima_ssi` `adpcm_ima_wav` `adpcm_ima_ws`  
`adpcm_ms` `adpcm_swf` `adpcm_yamaha` `aptx` `aptx_hd` `comfortnoise`  
`dfpwm` `g722` `g723_1` `g726` `g726le` `nellymoser`  
`pcm_s24daud` `pcm_vidc` `real_144` `roq_dpcm` `s302m` `tta`  
`wavpack` `wmav1` `wmav2`

> **对项目的影响**：Tab6 若提供「输出格式」下拉框，>   
> `mpeg4` / `wmv` / `flv` / `h263` / `theora` / `dvvideo` 这几项**必须过滤**，否则任务必然失败。

---

## 4. 音频

### 解码 217 项

**主流 / 具名格式 116 项**

`aac` `aac_fixed` `aac_latm` `ac3` `ac3_fixed` `alac`  
`als` `amrnb` `amrwb` `anull` `ape` `aptx`  
`aptx_hd` `atrac1` `atrac3` `atrac3al` `atrac3p` `atrac3pal`  
`atrac9` `binkaudio_dct` `binkaudio_rdft` `bmv_audio` `bonk` `cbd2_dpcm`  
`comfortnoise` `cook` `dca` `derf_dpcm` `dfpwm` `dolby_e`  
`dsicinaudio` `dss_sp` `dst` `dvaudio` `eac3` `eightsvx_exp`  
`eightsvx_fib` `evrc` `fastaudio` `ffwavesynth` `flac` `ftr`  
`g723_1` `g728` `g729` `gremlin_dpcm` `gsm` `gsm_ms`  
`hca` `hcom` `iac` `ilbc` `imc` `interplay_acm`  
`interplay_dpcm` `libopus` `libvorbis` `mace3` `mace6` `metasound`  
`mlp` `mp1` `mp1float` `mp2` `mp2float` `mp3`  
`mp3adu` `mp3adufloat` `mp3float` `mp3on4` `mp3on4float` `mpc7`  
`mpc8` `msnsiren` `nellymoser` `on2avc` `opus` `osq`  
`paf_audio` `qcelp` `qdm2` `qdmc` `qoa` `ra_144`  
`ra_288` `ralf` `rka` `roq_dpcm` `s302m` `sbc`  
`sdx2_dpcm` `shorten` `sipr` `siren` `smackaud` `sol_dpcm`  
`speex` `tak` `truehd` `truespeech` `tta` `twinvq`  
`vmdaudio` `vorbis` `wady_dpcm` `wavarc` `wavpack` `wmalossless`  
`wmapro` `wmav1` `wmav2` `wmavoice` `ws_snd1` `xan_dpcm`  
`xma1` `xma2`

**原始 PCM / ADPCM / DSD 101 项**

`adpcm_4xm` `adpcm_adx` `adpcm_afc` `adpcm_agm` `adpcm_aica` `adpcm_argo`  
`adpcm_circus` `adpcm_ct` `adpcm_dtk` `adpcm_ea` `adpcm_ea_maxis_xa` `adpcm_ea_r1`  
`adpcm_ea_r2` `adpcm_ea_r3` `adpcm_ea_xas` `adpcm_g722` `adpcm_g726` `adpcm_g726le`  
`adpcm_ima_acorn` `adpcm_ima_alp` `adpcm_ima_amv` `adpcm_ima_apc` `adpcm_ima_apm` `adpcm_ima_cunning`  
`adpcm_ima_dat4` `adpcm_ima_dk3` `adpcm_ima_dk4` `adpcm_ima_ea_eacs` `adpcm_ima_ea_sead` `adpcm_ima_escape`  
`adpcm_ima_hvqm2` `adpcm_ima_hvqm4` `adpcm_ima_iss` `adpcm_ima_magix` `adpcm_ima_moflex` `adpcm_ima_mtf`  
`adpcm_ima_oki` `adpcm_ima_pda` `adpcm_ima_qt` `adpcm_ima_rad` `adpcm_ima_smjpeg` `adpcm_ima_ssi`  
`adpcm_ima_wav` `adpcm_ima_ws` `adpcm_ima_xbox` `adpcm_ms` `adpcm_mtaf` `adpcm_n64`  
`adpcm_psx` `adpcm_psxc` `adpcm_sanyo` `adpcm_sbpro_2` `adpcm_sbpro_3` `adpcm_sbpro_4`  
`adpcm_swf` `adpcm_thp` `adpcm_thp_le` `adpcm_vima` `adpcm_xa` `adpcm_xmd`  
`adpcm_yamaha` `adpcm_zork` `dsd_lsbf` `dsd_lsbf_planar` `dsd_msbf` `dsd_msbf_planar`  
`pcm_alaw` `pcm_bluray` `pcm_dvd` `pcm_f16le` `pcm_f24le` `pcm_f32be`  
`pcm_f32le` `pcm_f64be` `pcm_f64le` `pcm_lxf` `pcm_mulaw` `pcm_s16be`  
`pcm_s16be_planar` `pcm_s16le` `pcm_s16le_planar` `pcm_s24be` `pcm_s24daud` `pcm_s24le`  
`pcm_s24le_planar` `pcm_s32be` `pcm_s32le` `pcm_s32le_planar` `pcm_s64be` `pcm_s64le`  
`pcm_s8` `pcm_s8_planar` `pcm_sga` `pcm_u16be` `pcm_u16le` `pcm_u24be`  
`pcm_u24le` `pcm_u32be` `pcm_u32le` `pcm_u8` `pcm_vidc`

### 编码 51 项

**主流 / 具名格式 22 项**

`aac` `aac_mf` `ac3` `ac3_fixed` `ac3_mf` `alac`  
`anull` `dca` `eac3` `flac` `libmp3lame` `libopus`  
`libvorbis` `mlp` `mp2` `mp2fixed` `mp3_mf` `opus`  
`ra_144` `sbc` `truehd` `vorbis`

**PCM 29 项**

`pcm_alaw` `pcm_bluray` `pcm_dvd` `pcm_f32be` `pcm_f32le` `pcm_f64be`  
`pcm_f64le` `pcm_mulaw` `pcm_s16be` `pcm_s16be_planar` `pcm_s16le` `pcm_s16le_planar`  
`pcm_s24be` `pcm_s24le` `pcm_s24le_planar` `pcm_s32be` `pcm_s32le` `pcm_s32le_planar`  
`pcm_s64be` `pcm_s64le` `pcm_s8` `pcm_s8_planar` `pcm_u16be` `pcm_u16le`  
`pcm_u24be` `pcm_u24le` `pcm_u32be` `pcm_u32le` `pcm_u8`

> AAC 用 FFmpeg **内置** `aac`（非 libfdk）；MP3 用 `libmp3lame`；>   
> Opus 用 `libopus`；Vorbis 用 `libvorbis`。

---

## 5. 字幕

|    | 数量 | 清单                                                                                                                                                                                                   |
| -- | -: | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 解码 | 23 | `ass` `ccaption` `dvbsub` `dvdsub` `idf` `jacosub` `microdvd` `movtext` `mpl2` `pgssub` `pjs` `realtext` `sami` `srt` `ssa` `stl` `subrip` `subviewer` `subviewer1` `text` `vplayer` `webvtt` `xsub` |
| 编码 | 11 | `ass` `dvbsub` `dvdsub` `movtext` `srt` `ssa` `subrip` `text` `ttml` `webvtt` `xsub`                                                                                                                 |

> 流拷贝（`c copy`）时字幕轨原样搬运，**不受编解码限制**。

---

## 6. 容器（封装格式）

| 容器                | 覆盖的扩展名                        | 解封装 |  封装 |
| ----------------- | ----------------------------- | :-: | :-: |
| MP4 / MOV         | `mov` `mp4` `m4a` `3gp` `m4v` |  ✅  |  ✅  |
| Matroska / WebM   | `mkv` `mka` `mks` `webm`      |  ✅  |  ✅  |
| AVI               | `avi`                         |  ✅  |  ✅  |
| Flash Video       | `flv`                         |  ✅  |  ✅  |
| MPEG-TS           | `ts` `m2ts` `mts`             |  ✅  |  ✅  |
| ASF / WMV         | `asf` `wmv` `wma`             |  ✅  |  ✅  |
| Ogg               | `ogg` `oga` `ogv`             |  ✅  |  ✅  |
| WAV / W64         | `wav` `w64`                   |  ✅  |  ✅  |
| MP3               | `mp3`                         |  ✅  |  ✅  |
| GIF / APNG / WebP | `gif` `apng` `webp`           |  ✅  |  ✅  |
| 图像序列              | `png` `/` `jpg` `序列`          |  ✅  |  ✅  |
| concat（拼接）        | `concat` `列表`                 |  ✅  |  ❌  |
| HLS / DASH        | `m3u8` `mpd`                  |  ✅  |  ✅  |
| MXF               | `mxf`                         |  ✅  |  ✅  |
| 原始流               | `h264` `hevc` `raw`           |  ✅  |  ✅  |


> `mov` 一个 demuxer 覆盖 `mp4 / m4a / 3gp / 3g2 / mj2`；`matroska` 覆盖 `mkv / mka / mks / webm`。
> 上表按 FFmpeg 内部组件名核对。

---

## 7. 硬件加速

### 硬件解码 hwaccel（51 项）

- **vaapi** — Linux VA-API，12 项：`av1_vaapi` `h263_vaapi` `h264_vaapi` `hevc_vaapi` `mjpeg_vaapi` `mpeg2_vaapi` `mpeg4_vaapi` `vc1_vaapi` `vp8_vaapi` `vp9_vaapi` `vvc_vaapi` `wmv3_vaapi`
- **nvdec** — NVIDIA NVDEC（独显硬解），11 项：`av1_nvdec` `h264_nvdec` `hevc_nvdec` `mjpeg_nvdec` `mpeg1_nvdec` `mpeg2_nvdec` `mpeg4_nvdec` `vc1_nvdec` `vp8_nvdec` `vp9_nvdec` `wmv3_nvdec`
- **d3d11va** — Direct3D 11，7 项：`av1_d3d11va` `h264_d3d11va` `hevc_d3d11va` `mpeg2_d3d11va` `vc1_d3d11va` `vp9_d3d11va` `wmv3_d3d11va`
- **d3d11va2** — Direct3D 11（二代接口），7 项：`av1_d3d11va2` `h264_d3d11va2` `hevc_d3d11va2` `mpeg2_d3d11va2` `vc1_d3d11va2` `vp9_d3d11va2` `wmv3_d3d11va2`
- **d3d12va** — Direct3D 12，7 项：`av1_d3d12va` `h264_d3d12va` `hevc_d3d12va` `mpeg2_d3d12va` `vc1_d3d12va` `vp9_d3d12va` `wmv3_d3d12va`
- **dxva2** — DirectX VA 2（旧接口），7 项：`av1_dxva2` `h264_dxva2` `hevc_dxva2` `mpeg2_dxva2` `vc1_dxva2` `vp9_dxva2` `wmv3_dxva2`

### 硬件编码器

| 厂商 / 接口 | H.264 | HEVC | AV1 | VP9 | VP8 | MPEG-2 |
|---|:--:|:--:|:--:|:--:|:--:|:--:|
| NVIDIA NVENC | ✅ | ✅ | ✅ | — | — | — |
| Intel QSV (libvpl) | ✅ | ✅ | ✅ | ✅ | — | ✅ |
| AMD AMF | ✅ | ✅ | ✅ | — | — | — |
| VA-API (Linux) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Direct3D 12 | ✅ | ✅ | — | — | — | — |
| MediaFoundation | ✅ | ✅ | ✅ | — | — | — |

> 任一硬件编码器初始化失败时**自动回退 `libx264`**（方案 §4.5 已规定）。

---

## 8. 协议（36 项）

`async` `cache` `concat` `concatf` `crypto` `data` `dtls` `fd`
`ffrtmphttp` `file` `ftp` `gopher` `gophers` `http` `httpproxy` `https`
`icecast` `ipfs_gateway` `ipns_gateway` `md5` `mmsh` `mmst` `pipe` `prompeg`
`rtmp` `rtmps` `rtmpt` `rtmpts` `rtp` `srtp` `subfile` `tcp`
`tee` `tls` `udp` `udplite`

> 构建时用 `--enable-protocol=file,pipe,http,https,rtmp,rtsp,tcp,udp` 限定白名单，
> 其余是这些协议依赖的辅助协议（`tls` / `httpproxy` / `cache` / `async` 等）。

---

## 9. 对 LRConvert 六个功能的影响

| 功能 | 状态 | 说明 |
|---|---|---|
| 视频截取 | ✅ 可用 | 纯流拷贝，只依赖 demuxer + muxer |
| 多视频连接 | ✅ 可用 | 同上（`concat` demuxer 已在列） |
| 音视频分离 | ✅ 可用 | 同上 |
| 音视频合并 | ✅ 可用 | 同上 |
| 批量压缩（M5） | ✅ 依赖齐备 | 解码全覆盖 + `libx264` + 硬件编码器全部可接 |
| 自定义编码（Tab6） | ⚠️ 需过滤选项 | MPEG-4 / WMV / FLV / H.263 / Theora / DV **无编码器** |

**关键点**：前四个功能只搬流、不重编码，**完全不受 `--disable-encoder` 影响**；
唯一约束是容器组合是否被支持。

---

## 10. 命名口径说明（重要）

上面所有名字是 **FFmpeg 内部组件名**（configure 输出形态），
与运行时 `avcodec_find_encoder_by_name()` 要传的 **codec name** 有系统性差异：

| 内部组件名 | 实际 codec name |
|---|---|
| `libvpx_vp8` | `libvpx` |
| `libvpx_vp9` | `libvpx-vp9` |
| `libaom_av1` | `libaom-av1` |
| `movtext` | `mov_text` |

**下划线 / 连字符不能直接照抄**。实现下拉框时应以运行时 `av_codec_iterate()` 
枚举到的 `codec->name` 为准（即 `FfmpegCapability` 采集到的清单）；
本文档用于**事前规划与人工核对**。

---

_本清单随 `vendor/ffmpeg` 内的库一同固定；若更换 FFmpeg 构建，需重新生成。_