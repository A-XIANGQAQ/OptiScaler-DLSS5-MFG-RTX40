# OptiScaler 中文说明

> **版本 `10.0.0-pre2`** ｜ 上游 `optiscaler/OptiScaler@d94fb15f`（2026-11-07）+ DLSS 神经渲染 + RTX 40 MFG 解锁 + 简体中文
> 上一版是 `10.0.0-pre1`（基于 2026-09 的 KleberMotta 分支）。这一版把上游 411 个提交全部并了进来。

按 **Insert** 打开菜单，**Page Up** 显示性能叠加层，**Page Down** 切换叠加层样式。
所有键位都能在菜单最下面的 **Keybinds** 里改。

---

## 一、先搞懂三件事

### 1. Input 和 Output

OptiScaler 是个中间人：它截住游戏发出的超分调用，转给你选的后端。

```
游戏 ──Input──▶ OptiScaler ──Output──▶ 实际干活的超分器
```

- **Input**＝游戏设置里选的那个（DLSS／FSR／XeSS）
- **Output**＝你在 OptiScaler 菜单里选的那个

所以「游戏里选 DLSS，实际跑 FSR 4」是成立的。菜单顶部那行
`D3D12 | Input: DLSS | Spoof: Off` 就是在告诉你这个状态。

> ⚠️ **必须先启用**：游戏设置里选一个超分方案，**并且读一个存档进到游戏里**。
> 在主菜单里超分往往不生效，OptiScaler 会显示「超分未启用」。

### 2. 帧生成也分 Input 和 Output

和超分是两套独立的东西：

- **FG Input**＝帧生成的数据来源（深度、运动矢量、无 HUD 帧从哪来）
- **FG Output**＝实际生成插值帧的后端
- **FG Nvngx Replacement**＝底层 DLSSG 提供者用哪个替换方案

组合起来能实现「游戏原本没有帧生成，却硬塞进去一个」。

### 3. `w/Dx12` 是什么

**`w/Dx12` ＝ "with DirectX 12"，意思是这个超分器不是原生跑在游戏的图形 API 上，
而是 OptiScaler 在后台开了一个 DirectX 12 设备，把纹理在两个 API 之间搬过去。**

看到它出现的位置：

```
D3D11 DLSS 2.4.12 w/Dx12 | Input: DLSS | Spoof: Off
                        ↑ 就是这个意思
```

**为什么要这么干？** 因为很多超分器只提供 DX12 接口。DX11 游戏想用它们，
只能借道 DX12：

| 你的游戏 | 原生能用的 | 加上 `w/Dx12` 才能用的 |
|---|---|---|
| DX11 | FSR 2.2.1、原生 DLSS、XeSS 2.x（仅 Arc） | XeSS、FSR 2.1.2／3.x、**FSR 4.x**、DLSS |
| Vulkan | FSR 2.1.2／3.1、原生 DLSS、XeSS 2.x | FSR 4.x、DLSS |

**代价**：`w/Dx12` 有性能损耗，官方文档说最多约 10%。而且它依赖两套 API 之间的
同步，驱动不稳时可能崩溃或出画面问题——所以菜单里会有专门的
**Dx11 with Dx12 Settings** / **Vulkan with Dx12 Settings** 分组让你调同步方式。

**对你特别重要的一点**：DX11 游戏想开本 fork 的**神经渲染（Neural Rendering）**，
超分器必须选 `dlss_12`（也就是带 `w/Dx12` 的那个）。原生 DX11 的 DLSS 不行。
Vulkan 游戏同理，需要 `ffx_12`／`dlss` 这类走 DX12 互操作的选项。

> 顺带一提：性能叠加层里的 `DXVK w/Dx12` 是另一回事，那是 Linux/Proton 下
> 走 DXVK 且启用了互操作时的显示，不是同一段逻辑。

---

## 一·五、这一版跟上一版差在哪

上一版停在 2026-09 的下游分支，这一版把上游 **411 个提交**（2026-09 → 2026-11）合并了进来。
菜单整体重做过，所以 90% 的截图对不上了，但功能没少。

### 菜单换了骨架

以前是一个长条列表，现在改成**卡片 + 标签页**：

- 顶部四个标签：**Main**（日常用）／**System**（纹理、兼容性）／**Advanced**（菜单外观、
  Quirks、状态）／外加一个可以自己拼的 **Custom**
- 每张卡片右上角有个图钉，钉住之后会出现在 **Custom** 标签页里，你可以只留自己常用的那几张
- 卡片是空的时候（当前上下文不适用）会自动不画，不再留一堆灰框

### 新增 / 变化的功能

- **XeFG（Intel Xe 帧生成）** 作为 FG Output 的一个新后端，开销最大但兼容性最好
- **HUDless 资源追踪**：翻了上游的 HUDFix 实现，UI 重影的处理路径变了；
  `System > Compatibility` 里能看当前判定
- **插件 SDK**：`plugins/PluginApi.h`，可以写自己的 ASI 插件挂进来（`Load ASI plugins`）
- **FSR 4 重构**：FSR4 的 preset / 色彩空间选项重新组织过，模型选择下放到 `FFX Settings` 卡片
- **Vulkan 重构**：`hkvkCreateDevice` 大量重写，加了 FIFO latest-ready 呈现模式检测、
  `VK_KHR_timeline_semaphore` 判定、DXGI-backed 调用方识别
- **逐着色器 GPU 耗时**：`Advanced > Status` 里能看到每个 shader 各自花多久
- **菜单外观**：圆角、背景模糊、模糊强度、阴影强度、字体、字号，都能调
- **FPS 叠加层**：行内容可选、图表高度、水平/垂直位置、刷新间隔、字距、文字阴影，
  还能「使用主题」或者「重置外观」
- **更新检查**：菜单里能直接检查新版本

### 我们这边保住的三块

合并的原则是：上游怎么改都跟，但下面三块是下游独有的，一个字符都不能丢。

| 功能 | 代码位置 | 说明 |
|---|---|---|
| **DLSS 神经渲染** | `OptiScaler/dlssnr/` | 上游完全没有这个目录 |
| **MFG 解锁（RTX 40）** | `OptiScaler/mfgunlock/` | 上游同样没有 |
| **简体中文** | `OptiScaler/menu/Localization.*` + `zh-CN.txt` | 靠 hook imgui 的文字绘制实现 |

这一版为了跟上新菜单，把两块的入口从「旧版列表里的两次调用」改成了**两个正式卡片**：

- `Upscaling` 标签下的 **Neural Rendering**
- `Frame Gen` 标签下的 **MFG Unlock (RTX 40)**

两块的设置在 **Custom** 标签页里也能钉住。

---

## 二、菜单逐项说明

### Upscalers（超分）

- **顶部那行**：图形 API、当前 Output 名称和版本、Input、Spoof（伪装）状态
- **Change Upscaler**：换 Output。部分后端需要重建管线，会卡一下
- **Use Reactive Mask as Transparency Mask**：把反应遮罩当透明遮罩用，
  只对 FSR 系后端有效，能改善部分游戏里半透明物体的处理
- **Dx11 / Vulkan with Dx12 Settings**：只有用了 `w/Dx12` 才出现，调两个 API
  之间的同步方式。默认值已经适合大多数情况，**只有出画面问题或崩溃时才动**

<details>
<summary><b>XeSS Settings</b></summary>

- **Network Models**：选 XeSS 的神经网络模型。官方注明「大概没什么实际作用」
- **Dump (Shift+Del)**：把输入输出纹理导到游戏目录，排查问题用
- **frames**：导多少帧
</details>

<details>
<summary><b>FFX Settings（FSR）</b></summary>

- **FFX Upscaler**：列出 FFX SDK 报告的可用超分器版本，可切换
- **Input Color Space**（FSR 4）：告诉它游戏用哪种色彩空间。
  `非线性 / sRGB` 可能提升画质但加重拖影，`PQ` 最罕见
- **FSR4 Preset**：**注意这不会改变游戏内的画质预设**，它是 FSR4 内部的模型预设，
  每个预设是为特定分辨率调校的
- **Upscaler Debug View**：调试视图，会显示运动矢量、深度、去遮挡遮罩等
- **Watermark**：给 FSR4 输出打水印
- **FSR 3.1 Presets**：`Stability`／`Motion`／`Default` 三个一键预设
- **FSR 3 Upscaler Manual Tuning**：手动调 速度因子／反应缩放／着色缩放／
  累积增量 等，**不懂就别动**，几个默认预设够用
</details>

<details>
<summary><b>DLSS Settings / DLSSD Settings</b></summary>

- **Render Presets Override**：覆盖 DLSS 的渲染预设（A~O 以及各代 Transformer）。
  改完要按 **Apply Changes**
- **Use Generic App Id with DLSS**：修复某些游戏里预设覆盖失效，需要重启游戏
- **Advanced DLSS Settings**：给每个画质档（DLAA／极高／画质／均衡／性能／极致性能）
  单独指定预设

> 如果显示「预设已被外部覆盖」，通常是因为你装了 NVIDIA App 或 Profile Inspector
> 之类的工具在改同一个设置。
</details>

### Frame Generation（帧生成）

先选 **FG Input** 和 **FG Output**，这是所有帧生成功能的总开关。

<details>
<summary><b>FG Input 各选项</b></summary>

| 选项 | 说明 |
|---|---|
| **None** | 不用帧生成输入 |
| **OptiFG (Upscaler)** | 拿超分器的数据自己造帧。**必须先启用超分**，且需要开 HUD 修复来防 UI 错乱 |
| **DLSSG via Streamline** | 游戏原生 DLSS 帧生成。需要在游戏里开 DLSS-FG，开箱支持无 HUD 帧，**仅限用 Streamline 的游戏** |
| **DLSSG via Nvngx** | 同上但走 nvngx 层，用于 FSR FG 的各变体 |
| **FSR 3.1 FG / 3.0 FG** | 游戏原生 FSR 帧生成，需要在游戏里开 FSR-FG |
</details>

<details>
<summary><b>FG Output 与替换方案</b></summary>

- **FSR FG**：FSR3/4 帧生成，RDNA4 会自动升级到 FSR4-FG
- **XeFG**：开销最大，但兼容性最好，对 HUD 处理也最好
- **DLSSG**：真正的 DLSS 帧生成
- **FG Nvngx Replacement**：`Nukems`（需 dlssg_to_fsr3 的 dll）、
  `Enabler`（需 dlss-enabler-headless.dll）、`FFX`、`Combo`（中间帧用 FFX、
  其余用 Enabler，能组合出 4x/6x）

菜单下半部分会列出所有可用的组合，鼠标悬停能看到各自要求。
</details>

- **Enable Frame Generation**：总开关
- **HUD 修复（Hudfix）**：解决 UI 元素被错误插值导致的拖影／重影。
  部分后端需要它，部分不需要
- **Debug View**：调试视图，显示运动矢量、深度、去遮挡遮罩等
- **Draw UI over FG**：把 UI 画在生成帧之上，需要无 HUD 帧和 UI 纹理
- **Frame Generation 矩形**：给上下有黑边的游戏调整生成区域
- **Frame Ahead**：允许帧生成领先游戏多少帧，能防止开/关切换出问题
- **Frame Pacing Tuning**：**帧节奏调优**。默认值已经调好，
  只有帧节奏不稳、卡顿时才动这几项

### Low Latency（低延迟）

- **低延迟方案**：`None`／`AntiLag 2`／`Reflex`／`XeLL`／`UeLowLatency`／`LatencyFlex`
- **Force State**：强制开启／关闭／跟随游戏设置
- **VRR Frame Cap Calculator**：帮你算出适合可变刷新率的帧率上限

> 想用 Reflex 但显卡是 AMD/Intel？需要 **fakenvapi** 来顶替（菜单里有对应分组）。

### Sharpness（锐化）

- **Override**：覆盖游戏给的锐化值
- **启用 RCAS/DA**：使用 AMD 的锐化，可加对比度参数
- **深度感知 (RCAS / DAS)**：越远的物体锐化越强，伪影更少但更吃性能
- **运动自适应锐化 (MAS)**：根据运动量增减锐化。**推荐用负值**（运动中降低锐化）
- **Advanced DA Parameters**：调深度阈值、边缘抑制强度等，**默认值通常就很好**

### Upscale Ratio Override / Output Scaling

- **超分比例覆盖**：1080p 屏上 1.5x 意味着内部分辨率 720p（1080 ÷ 1.5）
  - 可以「覆盖全部」或「按画质预设分别覆盖」
- **Output Scaling（伪超采样）**：先把画面超分到更高分辨率，再降采样回你的显示器分辨率。
  - 小于 1.0 → 省性能；大于 1.0 → 更锐利但更慢
  - 目标分辨率和总比例显示在底部，**总比例上限 3.0**

### Init Flags（初始化标志）

强制给 DLSS 加／去掉一些标记，修复特定问题：

- **Depth Inverted**：深度反转
- **Auto Exposure**：修复某些 UE 游戏暗部颜色问题
- **HDR**：修复某些游戏的紫色偏色
- **Jitter Cancellation**：游戏送来的运动数据已经预加过抖动时用
- **Display Res. MV**：某些游戏错误设置运动矢量尺寸导致动态模糊时，开关它试试
- **Disable Reactive Mask**：**默认开启**，因为关掉它往往问题更多

### Advanced Settings

- **DRS（动态分辨率缩放）**：覆盖游戏的最大／最小动态分辨率
- **Resource Barriers**：修正游戏送错状态的纹理，**主要针对 UE 游戏 + AMD 显卡**
  > ⚠️ **这里填错状态会崩溃**，不确定就别动
- **Root Signatures**：恢复计算／图形根签名，很少需要

### 其它

- **Magnifier**：屏幕放大镜，调试用
- **Active Quirks**：列出 OptiScaler 为这个游戏自动应用的兼容性调整（只读）
- **Logging**：日志级别、输出到文件／控制台
- **Menu Theme and Color**：菜单主题色、强调色、背景不透明度
- **FPS Overlay**：性能叠加层的位置、类型、缩放、是否用主题色
- **Upscaler Inputs**：强制哪些输入走哪条路径，**默认不用改**
- **V-Sync Settings**：强制垂直同步和同步间隔
- **Mipmap Bias**：覆盖纹理的 mipmap LOD。**负值更锐利，正值更模糊**，
  对性能有影响，改完需要切换一次分辨率才生效
- **Keybinds**：改所有快捷键

---

## 三、本 fork 独有的两个功能

### DLSS 神经渲染（DLSS Neural Rendering）

用 NVIDIA 的神经渲染模型，在超分输出之后、帧生成看到它之前，为画面合成细节。

**需要两个文件**放在 OptiScaler 旁边，文件名只差一个字符：

| 文件 | 说明 |
|---|---|
| `nvngx_dlssnr.dll` | NVIDIA 的模型，**本包已附带 310.9.16 版**（见下） |
| `nvngx.dll_dlssnr.dll` | 转发器，约 114 KB —— 本安装包自带 |

**前提**：RTX 50 系显卡（老卡跑不动这个模型）；DX11 游戏需要超分器选 `dlss_12`。

#### 附带的模型版本

本包里的 `nvngx_dlssnr.dll` 是 **310.9.16.0**，NVIDIA 官方签名版本（不是社区重编的）。
这是目前公开能拿到的最新一版，2026-10 随驱动发布，相对上一版 `310.8.2`：

- 同画质下神经渲染耗时可降约 38%（4080 SUPER 上 8.8ms → 5.4ms）
- 内含两套模型：`Original` 与 `Optimized FP8`，菜单里的 **Model preset** 就是切这个

校验值（自己核对一下，别装到假的）：

```
文件名   nvngx_dlssnr.dll
版本     310.9.16.0
大小     266,424,944 字节
SHA-256  2BC19BC09A7B89DD44F73E2AC0108FCC586E2154BDD0830F242F4F040E28789F
签名     Valid，CN = NVIDIA Corporation
```

**关于「6.21.14」**：那是 NVIDIA 的**驱动版本号**（621.14 WHQL，2026-11-05），
不是模型版本。621.14 这个驱动里**没有**再发新模型，它带的还是 10 月那个。
所以「最新的 DLSS 5 模型」＝ `nvngx_dlssnr.dll 310.9.16`，也就是本包附带的这个。

如果你的卡不是 RTX 50（比如 RTX 20/30/40 想试），官方签名版在非 Blackwell 上跑不起来，
需要用 openNR 从你自己提的文件重编的 compat-fp16 版：

```
openNR compat-fp16 版
大小     276,162,560 字节
SHA-256  CDFA6C1EC784867182164C7836F324F087B5C9F374D0C9FE46F54999FEABF3ED
签名     无（自编译，自校验 hash）
```

菜单里能调：

- **Apply the model**：总开关
- **Model resolution / Supersampling**：模型在多大分辨率上工作
- **Downscaler / Enlargement**：模型在低于画面尺寸运算时，结果怎么放大回来
- **Detail strength / Colour strength**：细节和色彩强度
- **Reversible proxy**：可逆代理模式（实验性）
- **Model preset / Style**：模型的几套处理配置
- **White point / Paper white / Trim / Anchor here**：**白点标定**。
  模型是在成品帧上训练的，而超分输出是线性的，所以必须告诉它「白色在哪」。
  这是效果好坏的关键，相关说明在菜单里都有悬浮提示
- **Compare（对比）**：Hold frame（定格）、Side by side（并排）、Wipe（擦除），
  用来对比开关前后的画面

### DLSS 多帧生成解锁（RTX 40）

NVIDIA 把 `nvngx_dlssg.dll` 里 2x 以上的功能锁给了 RTX 50 系。
这个功能**只在内存里**改写那两处架构判断，让游戏**自己**的 DLSS 帧生成
在 RTX 40 上也能跑 3x／4x（插件允许的话最高 6x）。

**磁盘上不做任何修改**——NVIDIA 是在加载时校验签名，不是加载之后。

用法：

1. 开启 **Enable multi-frame unlock (Ada)**
2. 保存，重启游戏
3. 把游戏里的 DLSS 帧生成打开
4. 如果游戏菜单里有 3x/4x/6x 就直接选；如果只有开／关，
   用 **Force frame multiplier** 滑块强制指定

**注意**：保持 OptiScaler 自己的帧生成**关闭**，这里由游戏的 DLSS-G 干活。

菜单里还能看到：

- **Temporal fix**：修正插值核，让额外帧落在各自时刻而不是全挤在中点
- **Force legacy software flip pacing**：3x/4x 画面卡死（但有声音）时才开
- **Raise the plugin's compiled frame ceiling**：抬高旧插件编译时的帧数上限，**默认关闭**
- 下面一整块是**遥测**：游戏请求了什么、实际呈现了多少帧、改写到没改写

---

## 四、出问题怎么办

1. **超分不生效** → 游戏里选好超分方案，**读存档进游戏**，别停在主菜单
2. **菜单打开但收不到输入** → 试试 `Alt + Insert`
3. **画面异常／崩溃** → 先把超分换回原生（不带 `w/Dx12`）的那个，
   再逐个排查 `w/Dx12` 的同步设置
4. **帧节奏乱** → 用 `Frame Pacing Tuning`，但一次只改一项
5. **看日志** → 打开 `Logging`，日志在游戏目录的 `OptiScaler.log`，
   里面有 `Localization:` 开头的行会告诉你语言表加载了多少条

> ⚠️ **不要在联机游戏里用**，可能触发反作弊导致封号。
