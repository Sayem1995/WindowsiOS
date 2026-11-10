// SPDX-License-Identifier: GPL-2.0-or-later
import SwiftUI
import UIKit

struct SessionView: View {
    @ObservedObject var vm: VMViewModel
    private let uiHaptic = UIImpactFeedbackGenerator(style: .light)

    var body: some View {
        GeometryReader { geo in
            let size = geo.size
            let laptopActive = vm.isLaptopLayoutActive(for: size)

            ZStack {
                Color(red: 0.02, green: 0.025, blue: 0.04).ignoresSafeArea()

                // Invisible UIKit host for iOS 27.1 UIHingeInteraction & system edge-gesture deferral
                HingeSensorView { statusRaw, angleDeg in
                    vm.updateHinge(statusRaw: statusRaw, angleDegrees: angleDeg, viewportSize: size)
                }
                .frame(width: 0, height: 0)
                .allowsHitTesting(false)

                if laptopActive {
                    surfaceLaptopClamshellLayout(geo: geo)
                        .transition(.opacity)
                } else {
                    surfaceStudioOpenLayout(geo: geo)
                        .transition(.opacity)
                }

                if !vm.isVMRunning {
                    surfaceHeroBootOverlay(viewportSize: size, laptopActive: laptopActive)
                        .padding(20)
                        .frame(
                            maxWidth: .infinity,
                            maxHeight: laptopActive ? size.height * 0.50 : .infinity,
                            alignment: .center
                        )
                        .frame(maxHeight: .infinity, alignment: laptopActive ? .top : .center)
                }
            }
            .onChange(of: size) { _, newSize in
                WinIOSBridge.shared.updateScreenSize(newSize, scale: UIScreen.main.scale)
            }
        }
        .ignoresSafeArea()
        .statusBarHidden(true)
        .persistentSystemOverlays(.hidden)
        .defersSystemGestures(on: .all)
    }

    // MARK: - 1. Sleek Midnight-Sapphire Surface Laptop Ultra Clamshell Mode (Folded)

    @ViewBuilder
    private func surfaceLaptopClamshellLayout(geo: GeometryProxy) -> some View {
        let totalW = geo.size.width
        let totalH = geo.size.height
        let lidH = (totalH * 0.50).rounded(.down)
        let deckH = max(180.0, totalH - lidH)
        let topCameraBezel = max(geo.safeAreaInsets.top > 20 ? geo.safeAreaInsets.top - 8 : 9.0, 9.0)

        VStack(spacing: 0) {
            // TOP 50%: Razor-Bezel Obsidian-Glass Surface Display Lid
            ZStack {
                // Anodized Midnight-Obsidian Lid Chassis + Diamond-Cut Outer Rim
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .fill(
                        LinearGradient(
                            colors: [
                                Color(red: 0.038, green: 0.046, blue: 0.072),
                                Color(red: 0.020, green: 0.024, blue: 0.038),
                            ],
                            startPoint: .top,
                            endPoint: .bottom
                        )
                    )
                    .overlay(
                        RoundedRectangle(cornerRadius: 12, style: .continuous)
                            .strokeBorder(
                                LinearGradient(
                                    colors: [
                                        Color(red: 0.64, green: 0.75, blue: 0.96).opacity(0.28),
                                        Color(red: 0.45, green: 0.56, blue: 0.78).opacity(0.10),
                                        Color.black.opacity(0.60),
                                    ],
                                    startPoint: .top,
                                    endPoint: .bottom
                                ),
                                lineWidth: 0.85
                            )
                    )

                VStack(spacing: 0) {
                    // Top Micro-Bezel with Windows Hello Studio Camera + IR Sensor Array
                    ZStack {
                        HStack(spacing: 7) {
                            Circle()
                                .fill(Color(red: 0.10, green: 0.12, blue: 0.18))
                                .frame(width: 3.5, height: 3.5)
                            ZStack {
                                Circle()
                                    .fill(Color(red: 0.05, green: 0.07, blue: 0.12))
                                    .frame(width: 5.5, height: 5.5)
                                    .overlay(Circle().strokeBorder(Color.white.opacity(0.16), lineWidth: 0.4))
                                Circle()
                                    .fill(Color(red: 0.45, green: 0.65, blue: 1.0).opacity(0.55))
                                    .frame(width: 1.6, height: 1.6)
                                    .offset(x: -0.8, y: -0.8)
                            }
                            Circle()
                                .fill(vm.isJITLive ? Color.white.opacity(0.32) : Color.orange.opacity(0.6))
                                .frame(width: 2.4, height: 2.4)
                        }
                    }
                    .frame(height: topCameraBezel)

                    // Edge-to-Edge PixelSense-Style Display Glass
                    MetalDisplayView(
                        trackpadMode: $vm.trackpadMode,
                        rightClickArmed: $vm.rightClickArmed,
                        keyboardVisible: .constant(false),
                        onThreeFingerTap: {
                            withAnimation(.spring(response: 0.28, dampingFraction: 0.88)) {
                                vm.postureMode = .fullScreen
                            }
                        }
                    )
                    .clipShape(RoundedRectangle(cornerRadius: 5.5, style: .continuous))
                    .overlay(
                        RoundedRectangle(cornerRadius: 5.5, style: .continuous)
                            .strokeBorder(Color.white.opacity(0.07), lineWidth: 0.5)
                    )
                    .padding(.horizontal, 6)

                    // Whisper-Minimal Lower Display Chin (zero toolbar clutter!)
                    ZStack {
                        HStack(spacing: 5) {
                            WindowsLogoMark(size: 7.5, color: Color.white.opacity(0.22))
                            Text("SURFACE")
                                .font(.system(size: 7.5, weight: .semibold))
                                .tracking(2.2)
                                .foregroundStyle(Color.white.opacity(0.22))
                        }

                        HStack {
                            Spacer()
                            // Discreet posture switch micro-trigger in bottom-right bezel corner
                            Button {
                                uiHaptic.impactOccurred(intensity: 0.5)
                                vm.cyclePostureMode(viewportSize: geo.size)
                            } label: {
                                Text(vm.hingeAngleDegrees.map { String(format: "%.0f°", $0) } ?? vm.postureMode.rawValue.uppercased())
                                    .font(.system(size: 8.0, weight: .medium, design: .monospaced))
                                    .foregroundStyle(Color.white.opacity(0.28))
                                    .padding(.horizontal, 6)
                                    .padding(.vertical, 1.5)
                            }
                            .buttonStyle(.plain)
                        }
                        .padding(.horizontal, 10)
                    }
                    .frame(height: 15)
                }
            }
            .frame(width: totalW, height: lidH)

            // BOTTOM 50%: Midnight-Sapphire CNC Aluminum Surface Keyboard + Haptic Glass Touchpad Deck
            LaptopDeckView(vm: vm)
                .frame(width: totalW, height: deckH)
        }
        .frame(width: totalW, height: totalH)
    }

    // MARK: - 2. Open / Unfolded Surface Book Studio Mode (180° Flat Inner / Landscape Canvas)

    @ViewBuilder
    private func surfaceStudioOpenLayout(geo: GeometryProxy) -> some View {
        let w = geo.size.width
        let h = geo.size.height
        let isPortrait = h > w
        let safeTop = max(geo.safeAreaInsets.top + 8.0, isPortrait ? 54.0 : 12.0)
        let safeBottom = max(geo.safeAreaInsets.bottom + 8.0, 12.0)

        ZStack {
            // Sculpted Obsidian-Sapphire Studio Backdrop (eliminates dead-flat black letterbox bars!)
            studioObsidianBackdrop(size: geo.size)

            if isPortrait {
                // Unfolded Portrait Studio Canvas: Framed PixelSense Display + Sleek Studio Telemetry & Quick Deck
                VStack(spacing: 14) {
                    Spacer(minLength: safeTop + (vm.controlBarVisible ? 44 : 8))

                    // Precision Diamond-Cut Framed Display
                    framedStudioDisplay(
                        maxWidth: w - 20,
                        maxHeight: min((w - 20) * 0.76, h * 0.64)
                    )

                    // Subtle Monospaced Hardware Spec Strip + Studio Quick Pad
                    surfaceStudioFooterStrip(viewportSize: geo.size)
                        .padding(.horizontal, 18)

                    Spacer(minLength: safeBottom)
                }
            } else {
                // Landscape Open Canvas: Maximum Edge-to-Edge Framed Display
                framedStudioDisplay(
                    maxWidth: w - 16,
                    maxHeight: h - 14
                )
            }

            // Floating Smoked-Obsidian Surface Command Pill
            VStack {
                if vm.controlBarVisible {
                    surfaceStudioCommandPill(viewportSize: geo.size)
                        .padding(.top, safeTop)
                        .padding(.horizontal, 14)
                        .transition(.move(edge: .top).combined(with: .opacity))
                } else {
                    HStack {
                        Spacer()
                        Button {
                            uiHaptic.impactOccurred()
                            withAnimation(.spring(response: 0.25, dampingFraction: 0.86)) {
                                vm.controlBarVisible = true
                            }
                        } label: {
                            HStack(spacing: 6) {
                                Circle()
                                    .fill(vm.isJITLive ? Color.white.opacity(0.85) : Color.orange)
                                    .frame(width: 5, height: 5)
                                WindowsLogoMark(size: 10, color: .white.opacity(0.90))
                            }
                            .padding(.horizontal, 12)
                            .padding(.vertical, 8)
                            .background(Color(red: 0.05, green: 0.065, blue: 0.11).opacity(0.82), in: Capsule())
                            .overlay(
                                Capsule()
                                    .strokeBorder(Color(red: 0.72, green: 0.82, blue: 1.0).opacity(0.24), lineWidth: 0.65)
                            )
                        }
                        .buttonStyle(.plain)
                        .padding(.top, safeTop)
                        .padding(.trailing, 16)
                    }
                }
                Spacer()
            }
        }
    }

    private func studioObsidianBackdrop(size: CGSize) -> some View {
        ZStack {
            LinearGradient(
                colors: [
                    Color(red: 0.024, green: 0.030, blue: 0.050),
                    Color(red: 0.012, green: 0.015, blue: 0.026),
                    Color(red: 0.030, green: 0.038, blue: 0.064),
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )

            // Subtle sculpted metallic light ribbon behind the display
            EllipticalGradient(
                colors: [
                    Color(red: 0.20, green: 0.28, blue: 0.46).opacity(0.22),
                    Color(red: 0.08, green: 0.12, blue: 0.22).opacity(0.06),
                    Color.clear,
                ],
                center: .init(x: 0.5, y: 0.44),
                startRadiusFraction: 0.05,
                endRadiusFraction: 0.62
            )
        }
        .ignoresSafeArea()
    }

    private func framedStudioDisplay(maxWidth: CGFloat, maxHeight: CGFloat) -> some View {
        let fbAspect: CGFloat = (vm.fbWidth > 0 && vm.fbHeight > 0)
            ? CGFloat(vm.fbWidth) / CGFloat(vm.fbHeight)
            : (4.0 / 3.0)
        let fittedW = min(maxWidth, maxHeight * fbAspect)
        let fittedH = fittedW / fbAspect

        return ZStack {
            RoundedRectangle(cornerRadius: 11, style: .continuous)
                .fill(Color.black)
                .shadow(color: .black.opacity(0.75), radius: 26, x: 0, y: 12)

            MetalDisplayView(
                trackpadMode: $vm.trackpadMode,
                rightClickArmed: $vm.rightClickArmed,
                keyboardVisible: $vm.systemKeyboardVisible,
                onThreeFingerTap: {
                    withAnimation(.spring(response: 0.25, dampingFraction: 0.86)) {
                        vm.controlBarVisible.toggle()
                    }
                }
            )
            .clipShape(RoundedRectangle(cornerRadius: 8.5, style: .continuous))
            .padding(3.5)
        }
        .frame(width: fittedW + 7, height: fittedH + 7)
        .overlay(
            RoundedRectangle(cornerRadius: 11, style: .continuous)
                .strokeBorder(
                    LinearGradient(
                        colors: [
                            Color(red: 0.72, green: 0.82, blue: 1.0).opacity(0.32),
                            Color(red: 0.44, green: 0.54, blue: 0.74).opacity(0.12),
                        ],
                        startPoint: .topLeading,
                        endPoint: .bottomTrailing
                    ),
                    lineWidth: 0.85
                )
        )
    }

    private func surfaceStudioFooterStrip(viewportSize: CGSize) -> some View {
        VStack(spacing: 10) {
            HStack {
                Text("DEVICE_NAME = SURFACE_DUO_ULTRA")
                    .font(.system(size: 9.5, weight: .medium, design: .monospaced))
                    .tracking(1.1)
                    .foregroundStyle(Color(red: 0.72, green: 0.80, blue: 0.95).opacity(0.55))

                Spacer()

                HStack(spacing: 5) {
                    Circle()
                        .fill(vm.isJITLive ? Color.white.opacity(0.75) : Color.orange)
                        .frame(width: 4.5, height: 4.5)
                    Text("WIN11 ARM64 · 4.5 GB")
                        .font(.system(size: 9.5, weight: .medium, design: .monospaced))
                        .foregroundStyle(Color.white.opacity(0.45))
                }
            }

            // Quick Studio Action Strip for Portrait Open Mode
            HStack(spacing: 8) {
                studioQuickTile(title: "Laptop Deck", icon: "laptopcomputer", prominent: true) {
                    withAnimation(.spring(response: 0.28, dampingFraction: 0.88)) {
                        vm.postureMode = .laptop
                        vm.systemKeyboardVisible = false
                    }
                    SceneOrientationController.alignForLaptopPostureIfLandscape(size: viewportSize)
                }

                studioQuickTile(title: "Antigravity", icon: "sparkles") {
                    vm.launchGuestAntigravity()
                }

                studioQuickTile(title: "Edge Web", icon: "globe") {
                    vm.launchGuestEdgeBrowser()
                }

                studioQuickTile(title: "Explorer", icon: "folder") {
                    WinIOSBridge.shared.sendKeyCombo(withModifiers: [NSNumber(value: 0xe3)], key: 0x08)
                }

                studioQuickTile(title: "Keyboard", icon: "keyboard", active: vm.systemKeyboardVisible) {
                    vm.systemKeyboardVisible.toggle()
                }
            }
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 12)
        .background(
            RoundedRectangle(cornerRadius: 14, style: .continuous)
                .fill(Color(red: 0.055, green: 0.070, blue: 0.115).opacity(0.72))
                .overlay(
                    RoundedRectangle(cornerRadius: 14, style: .continuous)
                        .strokeBorder(Color(red: 0.68, green: 0.78, blue: 0.98).opacity(0.16), lineWidth: 0.65)
                )
        )
    }

    private func studioQuickTile(
        title: String,
        icon: String,
        active: Bool = false,
        prominent: Bool = false,
        action: @escaping () -> Void
    ) -> some View {
        Button {
            uiHaptic.impactOccurred(intensity: 0.6)
            action()
        } label: {
            VStack(spacing: 5) {
                Image(systemName: icon)
                    .font(.system(size: 13.5, weight: .medium))
                Text(title)
                    .font(.system(size: 10, weight: .medium))
                    .lineLimit(1)
            }
            .foregroundStyle(
                active || prominent
                    ? Color(red: 0.05, green: 0.07, blue: 0.11)
                    : Color(red: 0.92, green: 0.95, blue: 1.0)
            )
            .frame(maxWidth: .infinity)
            .padding(.vertical, 9)
            .background(
                RoundedRectangle(cornerRadius: 9, style: .continuous)
                    .fill(
                        active || prominent
                            ? Color(red: 0.88, green: 0.93, blue: 1.0)
                            : Color(red: 0.10, green: 0.13, blue: 0.20).opacity(0.85)
                    )
            )
            .overlay(
                RoundedRectangle(cornerRadius: 9, style: .continuous)
                    .strokeBorder(Color.white.opacity(active || prominent ? 0.0 : 0.10), lineWidth: 0.5)
            )
        }
        .buttonStyle(.plain)
    }

    // MARK: - Smoked-Obsidian Surface Studio Floating Command Pill

    private func surfaceStudioCommandPill(viewportSize: CGSize) -> some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 6) {
                // Authentic 4-pane Start button
                Button {
                    uiHaptic.impactOccurred(intensity: 0.65)
                    WinIOSBridge.shared.sendWindowsKey()
                } label: {
                    HStack(spacing: 6) {
                        WindowsLogoMark(size: 10.5, color: Color(red: 0.05, green: 0.07, blue: 0.11))
                        Text("Start")
                            .font(.system(size: 11.5, weight: .semibold))
                            .foregroundStyle(Color(red: 0.05, green: 0.07, blue: 0.11))
                    }
                    .padding(.horizontal, 11)
                    .padding(.vertical, 6.5)
                    .background(Color(red: 0.90, green: 0.94, blue: 1.0), in: Capsule())
                }
                .buttonStyle(.plain)

                studioPillItem(icon: "laptopcomputer", label: "Laptop") {
                    withAnimation(.spring(response: 0.28, dampingFraction: 0.88)) {
                        vm.postureMode = .laptop
                        vm.systemKeyboardVisible = false
                    }
                    SceneOrientationController.alignForLaptopPostureIfLandscape(size: viewportSize)
                }

                studioPillItem(icon: "sparkles", label: "Antigravity") {
                    vm.launchGuestAntigravity()
                }

                studioPillItem(icon: "globe", label: "Edge") {
                    vm.launchGuestEdgeBrowser()
                }

                studioPillItem(icon: "keyboard", label: "Keys", active: vm.systemKeyboardVisible) {
                    vm.systemKeyboardVisible.toggle()
                }

                studioPillItem(
                    icon: vm.trackpadMode ? "rectangle.and.hand.point.up.left" : "hand.tap",
                    label: vm.trackpadMode ? "Trackpad" : "Touch",
                    active: vm.trackpadMode
                ) {
                    vm.trackpadMode.toggle()
                }

                studioPillItem(icon: "computermouse", label: "R-Click", active: vm.rightClickArmed) {
                    vm.rightClickArmed.toggle()
                }

                studioPillItem(icon: "waveform.path.ecg", label: "Task Mgr") {
                    WinIOSBridge.shared.sendKeyCombo(withModifiers: [NSNumber(value: 0xe0), NSNumber(value: 0xe1)], key: 0x29)
                }

                studioPillItem(icon: "power", label: "") {
                    WinIOSBridge.shared.pressPowerButton()
                }

                studioPillItem(icon: "chevron.up", label: "") {
                    withAnimation(.spring(response: 0.25, dampingFraction: 0.86)) {
                        vm.controlBarVisible = false
                    }
                }
            }
            .padding(.horizontal, 9)
            .padding(.vertical, 6.5)
            .background(
                Capsule()
                    .fill(Color(red: 0.045, green: 0.058, blue: 0.096).opacity(0.88))
            )
            .background(.ultraThinMaterial, in: Capsule())
            .overlay(
                Capsule()
                    .strokeBorder(Color(red: 0.72, green: 0.82, blue: 1.0).opacity(0.22), lineWidth: 0.65)
            )
            .shadow(color: .black.opacity(0.55), radius: 14, x: 0, y: 6)
        }
    }

    private func studioPillItem(
        icon: String,
        label: String,
        active: Bool = false,
        action: @escaping () -> Void
    ) -> some View {
        Button {
            uiHaptic.impactOccurred(intensity: 0.6)
            action()
        } label: {
            HStack(spacing: 4.5) {
                Image(systemName: icon)
                    .font(.system(size: 11.5, weight: .medium))
                if !label.isEmpty {
                    Text(label)
                        .font(.system(size: 11.5, weight: .medium))
                }
            }
            .foregroundStyle(
                active
                    ? Color(red: 0.05, green: 0.07, blue: 0.11)
                    : Color(red: 0.92, green: 0.95, blue: 1.0)
            )
            .padding(.horizontal, label.isEmpty ? 9 : 10.5)
            .padding(.vertical, 6.5)
            .background(
                Capsule().fill(
                    active
                        ? Color(red: 0.86, green: 0.92, blue: 1.0)
                        : Color.white.opacity(0.07)
                )
            )
        }
        .buttonStyle(.plain)
    }

    // MARK: - 3. SURFACE_LAPTOP_ULTRA Boot / Portal Hero Overlay

    private func surfaceHeroBootOverlay(viewportSize: CGSize, laptopActive: Bool) -> some View {
        VStack(spacing: laptopActive ? 11 : 16) {
            HStack(spacing: 9) {
                WindowsLogoMark(size: laptopActive ? 14 : 17, color: .white)
                Text("SURFACE LAPTOP ULTRA")
                    .font(.system(size: laptopActive ? 13.5 : 15.5, weight: .semibold, design: .monospaced))
                    .tracking(2.0)
                    .foregroundStyle(.white)
            }

            Text("DEVICE_NAME = IPHONE_DUO_ARM64")
                .font(.system(size: 10, weight: .regular, design: .monospaced))
                .tracking(1.4)
                .foregroundStyle(Color(red: 0.72, green: 0.82, blue: 1.0).opacity(0.65))

            Text(vm.vmStatusText)
                .font(.system(size: 12, weight: .regular))
                .foregroundStyle(.white.opacity(0.82))
                .multilineTextAlignment(.center)

            if vm.isUnpacking {
                ProgressView(value: vm.unpackProgress, total: 1.0)
                    .tint(.white)
                    .frame(maxWidth: 260)
            }

            HStack(spacing: 14) {
                HStack(spacing: 5) {
                    Circle()
                        .fill(vm.isJITLive ? Color.white : Color.orange)
                        .frame(width: 5.5, height: 5.5)
                    Text(vm.isJITLive ? "TXM JIT LIVE" : vm.jitStatusText.uppercased())
                        .font(.system(size: 10, weight: .medium, design: .monospaced))
                        .foregroundStyle(.white.opacity(0.80))
                }

                Text("·")
                    .foregroundStyle(.white.opacity(0.35))

                Text("\(vm.availableMemoryMB) MB HEADROOM")
                    .font(.system(size: 10, weight: .medium, design: .monospaced))
                    .foregroundStyle(.white.opacity(0.60))
            }

            if !vm.isUnpacking {
                HStack(spacing: 10) {
                    if !vm.isJITLive {
                        Button {
                            vm.requestStikDebugJIT()
                        } label: {
                            Text("ENABLE JIT")
                                .font(.system(size: 11, weight: .semibold, design: .monospaced))
                                .tracking(1.0)
                                .foregroundStyle(.white)
                                .padding(.horizontal, 14)
                                .padding(.vertical, 9)
                                .background(Color.white.opacity(0.12), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
                                .overlay(RoundedRectangle(cornerRadius: 8, style: .continuous).strokeBorder(Color.white.opacity(0.24), lineWidth: 0.6))
                        }
                        .buttonStyle(.plain)
                    }

                    Button {
                        vm.startOrRetry()
                    } label: {
                        Text(vm.isJITLive ? "BOOT WINDOWS 11" : "RETRY BOOT")
                            .font(.system(size: 11, weight: .semibold, design: .monospaced))
                            .tracking(1.1)
                            .foregroundStyle(Color(red: 0.04, green: 0.05, blue: 0.09))
                            .padding(.horizontal, 16)
                            .padding(.vertical, 9)
                            .background(Color(red: 0.92, green: 0.95, blue: 1.0), in: RoundedRectangle(cornerRadius: 8, style: .continuous))
                    }
                    .buttonStyle(.plain)
                }
            }
        }
        .padding(laptopActive ? 18 : 26)
        .frame(maxWidth: 420)
        .background(
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .fill(Color(red: 0.04, green: 0.052, blue: 0.088).opacity(0.92))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 16, style: .continuous)
                .strokeBorder(
                    LinearGradient(
                        colors: [
                            Color(red: 0.74, green: 0.84, blue: 1.0).opacity(0.32),
                            Color(red: 0.40, green: 0.50, blue: 0.72).opacity(0.10),
                        ],
                        startPoint: .topLeading,
                        endPoint: .bottomTrailing
                    ),
                    lineWidth: 0.8
                )
        )
        .shadow(color: .black.opacity(0.70), radius: 24, x: 0, y: 10)
    }
}
