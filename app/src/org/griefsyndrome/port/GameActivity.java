package org.griefsyndrome.port;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.media.AudioManager;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.os.Environment;
import android.provider.Settings;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Color;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.util.SparseIntArray;
import android.view.Display;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.TextView;

public final class GameActivity extends Activity implements SurfaceHolder.Callback {
    private static volatile GameActivity current;
    private PortSettings settings;
    private TouchControlsView controls;
    private TextView loading;
    private TextView menuButton;
    private FrameLayout root;
    private LanLobby lanLobby;
    private LanDialog lanDialog;
    private android.app.Dialog editorDialog;
    private AlertDialog confirmDialog;
    private AlertDialog qaDialog;
    private org.griefsyndrome.port.launcher.Launch launch;
    private boolean gameplayVisible;
    private boolean pauseVisible;
    private Object backCallback;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private DisplayManager displayManager;
    private final DisplayManager.DisplayListener displayListener = new DisplayManager.DisplayListener() {
        @Override
        public void onDisplayAdded(int displayId) {}
        @Override
        public void onDisplayRemoved(int displayId) {}
        @Override
        public void onDisplayChanged(int displayId) {
            if (getWindowManager().getDefaultDisplay() != null && displayId == getWindowManager().getDefaultDisplay().getDisplayId()) applyFpsLimit();
        }
    };
    private final SparseIntArray gamepadPlayers = new SparseIntArray();
    private final SparseIntArray directionStates = new SparseIntArray();
    private boolean started;
    private boolean activityPaused;
    private boolean portMenuOpen;
    private boolean storageReady;
    private boolean storageRequested;
    private SurfaceHolder surfaceHolder;
    private WifiManager.MulticastLock multicastLock;
    private static final int STORAGE_REQUEST = 41;
    private static final int LAN_REQUEST = 42;
    private static final String LAN_PERMISSION = "android.permission.ACCESS_LOCAL_NETWORK";
    private static final SparseIntArray keySources = new SparseIntArray();

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        launch = org.griefsyndrome.port.launcher.Launch.attach(this);
        System.loadLibrary("griefsyndrome");
        current = this;
        settings = new PortSettings(this, getIntent().getStringExtra("userdata"));
        setVolumeControlStream(AudioManager.STREAM_MUSIC);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        WindowManager.LayoutParams windowParameters = getWindow().getAttributes();
        if (Build.VERSION.SDK_INT >= 30) {
            windowParameters.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
            getWindow().setDecorFitsSystemWindows(false);
        } else if (Build.VERSION.SDK_INT >= 28) {
            windowParameters.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        getWindow().setAttributes(windowParameters);
        hideSystemUi();
        root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        if (Build.VERSION.SDK_INT >= 29) root.setForceDarkAllowed(false);
        root.setOnApplyWindowInsetsListener((view, insets) -> {
            int left, top, right, bottom;
            if (Build.VERSION.SDK_INT >= 30) {
                left = insets.getInsets(WindowInsets.Type.systemBars()).left;
                top = insets.getInsets(WindowInsets.Type.systemBars()).top;
                right = insets.getInsets(WindowInsets.Type.systemBars()).right;
                bottom = insets.getInsets(WindowInsets.Type.systemBars()).bottom;
            } else {
                left = insets.getSystemWindowInsetLeft();
                top = insets.getSystemWindowInsetTop();
                right = insets.getSystemWindowInsetRight();
                bottom = insets.getSystemWindowInsetBottom();
            }
            if (Build.VERSION.SDK_INT >= 28 && insets.getDisplayCutout() != null) {
                left = Math.max(left, insets.getDisplayCutout().getSafeInsetLeft());
                top = Math.max(top, insets.getDisplayCutout().getSafeInsetTop());
                right = Math.max(right, insets.getDisplayCutout().getSafeInsetRight());
                bottom = Math.max(bottom, insets.getDisplayCutout().getSafeInsetBottom());
            }
            controls.setSafeInsets(left, top, right, bottom);
            FrameLayout.LayoutParams parameters = (FrameLayout.LayoutParams) menuButton.getLayoutParams();
            if (parameters != null) {
                parameters.leftMargin = dp(12) + left;
                parameters.topMargin = dp(12) + top;
                menuButton.setLayoutParams(parameters);
            }
            nativeReleaseAll();
            return insets;
        });
        SurfaceView surface = new SurfaceView(this);
        surface.setZOrderMediaOverlay(true);
        surface.getHolder().addCallback(this);
        root.addView(surface, new FrameLayout.LayoutParams(-1, -1));
        controls = new TouchControlsView(this, settings);
        controls.setRussian(!"jp".equals(getIntent().getStringExtra("language")));
        controls.setListener(() -> {
            if (editorDialog != null) {
                editorDialog.dismiss();
            } else {
                updateOverlayVisibility();
                if (!activityPaused) nativePause(false);
            }
        });
        controls.setVisibility(View.INVISIBLE);
        nativeControlLabels(settings.controlLabels());
        root.addView(controls, new FrameLayout.LayoutParams(-1, -1));
        menuButton = new TextView(this);
        menuButton.setText("II");
        menuButton.setContentDescription("Пауза; удержание — дополнительные настройки");
        menuButton.setTextColor(0xCCFFFFFF);
        menuButton.setTextSize(22);
        menuButton.setGravity(Gravity.CENTER);
        menuButton.setBackgroundColor(0x5520242C);
        menuButton.setOnClickListener(view -> tap(nativeTouchBinding(8)));
        menuButton.setOnLongClickListener(view -> {
            showMenu();
            return true;
        });
        FrameLayout.LayoutParams menuLayout = new FrameLayout.LayoutParams(dp(44), dp(40));
        menuLayout.gravity = Gravity.TOP | Gravity.LEFT;
        menuLayout.setMargins(dp(12), dp(12), 0, 0);
        menuButton.setVisibility(View.GONE);
        root.addView(menuButton, menuLayout);
        loading = new TextView(this);
        loading.setText("Загрузка…");
        loading.setTextColor(0xFFE6EDF7);
        loading.setTextSize(20);
        loading.setGravity(Gravity.RIGHT | Gravity.BOTTOM);
        loading.setPadding(dp(24), dp(24), dp(24), dp(24));
        loading.setBackgroundColor(0xFF101218);
        root.addView(loading, new FrameLayout.LayoutParams(-1, -1));
        setContentView(root);
        applyFpsLimit();
        if (Build.VERSION.SDK_INT >= 33) {
            android.window.OnBackInvokedCallback back = this::handleBack;
            backCallback = back;
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, back);
        }
        handler.postDelayed(new Runnable() {
            @Override
            public void run() {
                if (isFinishing()) return;
                if (nativeLoadReady()) {
                    loading.setVisibility(View.GONE);
                    launch.firstFrame(root);
                } else {
                    handler.postDelayed(this, 250);
                }
            }
        }, 250);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void updateOverlayVisibility() {
        boolean active = gameplayVisible && !pauseVisible && !portMenuOpen && lanLobby == null && editorDialog == null;
        menuButton.setVisibility(active ? View.VISIBLE : View.GONE);
        if (editorDialog == null)
            controls.setVisibility(active && settings.controlsVisible() ? View.VISIBLE : View.INVISIBLE);
    }

    private void applyFpsLimit() {
        if (!storageReady) return;
        Display display = getWindowManager().getDefaultDisplay();
        if (display == null) return;
        Display.Mode current = display.getMode();
        java.util.TreeSet<Integer> supported = new java.util.TreeSet<>();
        for (Display.Mode mode : display.getSupportedModes()) {
            int rate = Math.round(mode.getRefreshRate());
            if (mode.getPhysicalWidth() == current.getPhysicalWidth() &&
                mode.getPhysicalHeight() == current.getPhysicalHeight() && rate > 0) supported.add(rate);
        }
        if (supported.isEmpty()) return;
        float maximum = supported.last();
        float selected = settings.fpsLimit(maximum);
        if (selected != 0.0f && !supported.contains(Math.round(selected))) {
            selected = maximum;
            settings.setFpsLimit(selected);
        }
        selected = selected == 0.0f ? 0.0f : Math.round(selected);
        float[] rates = new float[supported.size()];
        int index = 0;
        for (Integer rate : supported) rates[index++] = rate;
        Display.Mode best = null;
        for (Display.Mode mode : display.getSupportedModes()) {
            if (mode.getPhysicalWidth() != current.getPhysicalWidth() ||
                mode.getPhysicalHeight() != current.getPhysicalHeight()) continue;
            if (selected == 0.0f) {
                if (best == null || mode.getRefreshRate() > best.getRefreshRate()) best = mode;
            } else if (Math.round(mode.getRefreshRate()) == Math.round(selected) &&
                (best == null || Math.abs(mode.getRefreshRate() - selected) < Math.abs(best.getRefreshRate() - selected))) {
                best = mode;
            }
        }
        if (best != null && best.getModeId() != current.getModeId()) {
            WindowManager.LayoutParams parameters = getWindow().getAttributes();
            parameters.preferredDisplayModeId = best.getModeId();
            getWindow().setAttributes(parameters);
        }
        float active = display.getRefreshRate();
        float effective = selected == 0.0f ? active : Math.min(selected, active);
        nativeFpsRates(rates);
        nativeFpsChoice(selected);
        nativeFpsLimit(effective);
        String profile = getIntent().getStringExtra("userdata");
        float qaLimit = getIntent().getFloatExtra("qaFpsLimit", 0.0f);
        if (profile != null && !profile.isEmpty() && qaLimit > 0.0f) nativeFpsLimit(qaLimit);
    }

    private void openTouchLayout() {
        if (editorDialog != null) return;
        menuButton.setVisibility(View.GONE);
        nativePause(true);
        root.removeView(controls);
        editorDialog = new android.app.Dialog(this, android.R.style.Theme_Black_NoTitleBar_Fullscreen);
        editorDialog.setContentView(controls, new FrameLayout.LayoutParams(-1, -1));
        editorDialog.setOnDismissListener(dialog -> {
            if (controls.isEditing()) controls.setEditing(false);
            if (controls.getParent() instanceof android.view.ViewGroup)
                ((android.view.ViewGroup) controls.getParent()).removeView(controls);
            root.addView(controls, new FrameLayout.LayoutParams(-1, -1));
            editorDialog = null;
            updateOverlayVisibility();
            if (!activityPaused) nativePause(false);
        });
        controls.setVisibility(View.VISIBLE);
        editorDialog.show();
        controls.setEditing(true);
    }

    static void onPortSetting(int event, float value) {
        GameActivity activity = current;
        if (activity == null) return;
        activity.runOnUiThread(() -> {
            if (event == 6) {
                activity.settings.setFpsLimit(value);
                activity.applyFpsLimit();
            } else if (event == 12) {
                activity.openTouchLayout();
            } else if (event == 16) {
                activity.settings.controlLabels(value != 0.0f);
                activity.controls.setLabelsVisible(value != 0.0f);
            } else if (event == 15) {
                activity.portMenuOpen = value != 0;
                activity.updateOverlayVisibility();
            } else if (event == 17) {
                activity.showLanDialog();
            } else if (event == 20) {
                activity.loading.setVisibility(View.GONE);
                if (activity.lanLobby == null) {
                    activity.lanLobby = new LanLobby(activity, activity.settings);
                    activity.lanLobby.show(activity.root);
                }
                activity.updateOverlayVisibility();
            } else if (event == 21 && activity.lanLobby != null) {
                activity.lanLobby.close(activity.root);
                activity.lanLobby = null;
                activity.updateOverlayVisibility();
            } else if (event == 22) {
                activity.gameplayVisible = value != 0;
                activity.updateOverlayVisibility();
            } else if (event == 23) {
                activity.pauseVisible = value != 0;
                activity.updateOverlayVisibility();
            } else if (event == 24 || event == 25) {
                activity.showConfirmation(event == 24 ? 0 : 1);
            } else if (event == 26) {
                activity.showQaDialog();
            } else if (event == 27 && activity.settings.qaFlags() != (int)value) {
                activity.settings.qaFlags((int)value);
            } else if (event == 28 && activity.settings.showHomuraHint()) {
                android.widget.Toast.makeText(activity, !"jp".equals(activity.getIntent().getStringExtra("language")) ?
                    "Зажмите портрет Хомуры, чтобы сменить персонажа. Короткий тап подтверждает выбор." :
                    "ほむらのポートレートを長押しでキャラクター切替。タップで決定。",
                    android.widget.Toast.LENGTH_LONG).show();
            }
        });
    }

    private void showQaDialog() {
        if (qaDialog != null) return;
        int flags = nativeQaFlags();
        boolean[] checked = { (flags & 1) != 0, (flags & 2) != 0, (flags & 4) != 0 };
        nativeReleaseAll();
        qaDialog = new AlertDialog.Builder(this).setTitle("QA — одиночная игра")
            .setMultiChoiceItems(new String[]{"Разблокировать все уровни", "Бессмертие", "One shot kill"},
                checked, (dialog, which, enabled) -> checked[which] = enabled)
            .setPositiveButton("Применить", (dialog, which) -> {
                int selected = (checked[0] ? 1 : 0) | (checked[1] ? 2 : 0) | (checked[2] ? 4 : 0);
                nativeQaSet(selected);
                settings.qaFlags(selected);
                android.widget.Toast.makeText(this, selected == 0 ? "QA выключен. LAN доступен." :
                    "QA включён. Для LAN выключите все три опции.", android.widget.Toast.LENGTH_LONG).show();
            })
            .setNegativeButton("Отмена", null)
            .setOnDismissListener(dialog -> { qaDialog = null; hideSystemUi(); })
            .show();
        qaDialog.setCanceledOnTouchOutside(false);
    }

    private void showConfirmation(int kind) {
        if (confirmDialog != null) return;
        confirmDialog = new AlertDialog.Builder(this)
            .setTitle(kind == 0 ? "Сброс настроек" : "Выход из игры")
            .setMessage(kind == 0 ? "Вернуть все настройки по умолчанию?" : "Выйти из игры в лаунчер?")
            .setPositiveButton(kind == 0 ? "Сбросить" : "Выйти", (dialog, which) -> nativeMenuConfirm(kind, true))
            .setNegativeButton("Отмена", (dialog, which) -> nativeMenuConfirm(kind, false))
            .setOnCancelListener(dialog -> nativeMenuConfirm(kind, false))
            .setOnDismissListener(dialog -> confirmDialog = null)
            .show();
    }

    private void hideSystemUi() {
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN |
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        if (Build.VERSION.SDK_INT >= 30) {
            android.view.WindowInsetsController controller = getWindow().getDecorView().getWindowInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.systemBars());
                controller.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            hideSystemUi();
        } else {
            controls.cancelTouch();
            nativeReleaseAll();
        }
    }

    private void setFrameRateHint(SurfaceHolder holder) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && getWindowManager().getDefaultDisplay() != null) {
            float selected = settings.fpsLimit();
            float rate = selected > 0.0f ? selected : getWindowManager().getDefaultDisplay().getRefreshRate();
            holder.getSurface().setFrameRate(rate, Surface.FRAME_RATE_COMPATIBILITY_DEFAULT);
        }
    }

    private boolean hasStorageAccess() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        return checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    private void ensureStorageAccess() {
        if (hasStorageAccess()) {
            if (!storageReady) {
                storageReady = true;
                prepareStorageAndStart();
            }
            return;
        }
        if (storageRequested) return;
        storageRequested = true;
        loading.setText("Для сохранений нужен доступ к файлам");
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                startActivityForResult(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:" + getPackageName())), STORAGE_REQUEST);
            } catch (android.content.ActivityNotFoundException error) {
                startActivityForResult(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION), STORAGE_REQUEST);
            }
        } else {
            requestPermissions(new String[] {android.Manifest.permission.READ_EXTERNAL_STORAGE,
                android.Manifest.permission.WRITE_EXTERNAL_STORAGE}, STORAGE_REQUEST);
        }
    }

    private void prepareStorageAndStart() {
        try {
            settings.migrateLegacyData();
            SessionLog.begin(this, settings.dataDirectory());
        } catch (Exception error) {
            android.util.Log.e("GriefSyndrome", "Cannot prepare the persistent profile", error);
            launch.failed("Cannot prepare the persistent profile: " + error.getMessage());
            loading.setText("Не удалось подготовить профиль сохранений");
            return;
        }
        applyFpsLimit();
        nativeQaSet(settings.qaFlags());
        if (surfaceHolder != null) startGame(surfaceHolder);
    }

    private void startGame(SurfaceHolder holder) {
        int width = holder.getSurfaceFrame().width(), height = holder.getSurfaceFrame().height();
        // The first surface can still be portrait while Android rotates the activity.
        if (started || !storageReady || height <= 0 || width < height) return;
        java.io.File gameFolder = launch.folder("game");
        String problem = PortSettings.gameDataProblem(gameFolder);
        if (problem != null) {
            launch.failed(problem);
            loading.setText(problem + "\nУстановите данные через лаунчер");
            return;
        }
        started = true;
        String language = getIntent().getStringExtra("language");
        if (language == null) {
            boolean russian = PortSettings.hasRussianData(this) && "1".equals(org.griefsyndrome.port.launcher.State.get(
                this, org.griefsyndrome.port.launcher.State.CHOICES, "option.russian", "1"));
            language = russian ? "ru" : "jp";
        }
        launch.starting();
        nativeStart(gameFolder.getAbsolutePath(), settings.dataDirectory().getAbsolutePath(),
            gameFolder.getAbsolutePath(), language,
            width, height, launch.skipIntro(),
            getIntent().getBooleanExtra("lan", false),
            getIntent().getBooleanExtra("lanHost", settings.lanHost()),
            getIntent().getStringExtra("lanAddress") != null ? getIntent().getStringExtra("lanAddress") : settings.lanAddress(),
            getIntent().getIntExtra("lanPort", settings.lanPort()),
            getIntent().getIntExtra("lanPlayers", settings.lanPlayers()),
            getIntent().getBooleanExtra("verbose", false));
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        surfaceHolder = holder;
        setFrameRateHint(holder);
        nativeSurface(holder.getSurface());
        if (storageReady) prepareStorageAndStart();
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (request == STORAGE_REQUEST) {
            storageRequested = false;
            ensureStorageAccess();
        } else {
            super.onActivityResult(request, result, data);
        }
    }

    @Override
    public void onRequestPermissionsResult(int request, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(request, permissions, results);
        if (request == STORAGE_REQUEST) {
            storageRequested = false;
            ensureStorageAccess();
        } else if (request == LAN_REQUEST) {
            if (checkSelfPermission(LAN_PERMISSION) == PackageManager.PERMISSION_GRANTED) {
                showLanDialog();
            } else {
                new AlertDialog.Builder(this).setTitle("LAN-мультиплеер")
                    .setMessage("Для поиска комнат и совместной игры разрешите доступ к локальной сети в настройках приложения.")
                    .setPositiveButton("Настройки", (dialog, which) -> startActivity(new Intent(
                        Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:" + getPackageName()))))
                    .setNegativeButton("Отмена", null).show();
            }
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        surfaceHolder = holder;
        controls.cancelTouch();
        nativeReleaseAll();
        setFrameRateHint(holder);
        nativeSurface(holder.getSurface());
        if (storageReady) handler.post(() -> {
            if (surfaceHolder == holder) startGame(holder);
        });
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        controls.cancelTouch();
        nativeReleaseAll();
        nativeSurface(null);
        if (surfaceHolder == holder) surfaceHolder = null;
    }

    @Override
    protected void onResume() {
        super.onResume();
        activityPaused = false;
        hideSystemUi();
        updateOverlayVisibility();
        if (displayManager == null) displayManager = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        displayManager.registerDisplayListener(displayListener, handler);
        if (multicastLock == null) {
            WifiManager manager = (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
            multicastLock = manager.createMulticastLock("GriefSyndrome");
            multicastLock.setReferenceCounted(false);
        }
        multicastLock.acquire();
        ensureStorageAccess();
        if (storageReady) applyFpsLimit();
        nativePause(controls.isEditing());
    }

    @Override
    protected void onPause() {
        activityPaused = true;
        if (lanDialog != null) { lanDialog.close(); lanDialog = null; }
        directionStates.clear();
        keySources.clear();
        controls.cancelTouch();
        nativeReleaseAll();
        nativePause(true);
        if (multicastLock != null && multicastLock.isHeld()) multicastLock.release();
        if (displayManager != null) displayManager.unregisterDisplayListener(displayListener);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        keySources.clear();
        if (Build.VERSION.SDK_INT >= 33 && backCallback != null)
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback((android.window.OnBackInvokedCallback) backCallback);
        if (confirmDialog != null) {
            confirmDialog.dismiss();
            confirmDialog = null;
        }
        if (editorDialog != null) {
            editorDialog.dismiss();
            editorDialog = null;
        }
        if (lanLobby != null) {
            lanLobby.close(root);
            lanLobby = null;
        }
        if (current == this) current = null;
        super.onDestroy();
    }

    private static synchronized void keyFromSource(int key, int source, boolean down) {
        if (source == 0) return;
        int old = keySources.get(key, 0);
        int next = down ? old | source : old & ~source;
        if (next == old) return;
        if (next == 0) keySources.delete(key); else keySources.put(key, next);
        if ((old == 0) != (next == 0)) nativeKeyFromSource(key, next != 0, source);
    }

    static void touchKey(int key, boolean down) { keyFromSource(key, 4, down); }

    private static int inputSource(KeyEvent event) {
        if (event.getDevice() == null) return 1;
        int sources = event.getDevice().getSources();
        return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
            (sources & InputDevice.SOURCE_CLASS_JOYSTICK) != 0 ? 2 : 1;
    }

    private void tap(int key) {
        keyFromSource(key, 8, true);
        keyFromSource(key, 8, false);
    }

    private void showMenu() {
        if (controls.isEditing()) return;
        String[] items = {
            "Пауза / продолжить",
            settings.controlsVisible() ? "Скрыть кнопки" : "Показать кнопки",
            "Раскладка и размер кнопок",
            "Прозрачность кнопок",
            settings.controlsAutoHide() ? "Не скрывать при геймпаде" : "Скрывать при геймпаде",
            "Сменить локализацию",
            "Справка по управлению",
            "Выйти в лаунчер"
        };
        new AlertDialog.Builder(this).setTitle("Grief Syndrome").setItems(items, (dialog, which) -> {
            switch (which) {
                case 0: tap(nativeTouchBinding(8)); break;
                case 1:
                    boolean visible = !settings.controlsVisible();
                    settings.controlsVisible(visible);
                    controls.setVisibility(visible ? View.VISIBLE : View.INVISIBLE);
                    break;
                case 2: openTouchLayout(); break;
                case 3: showAlpha(); break;
                case 4:
                    boolean autoHide = !settings.controlsAutoHide();
                    settings.controlsAutoHide(autoHide);
                    controls.setVisibility(autoHide ? View.INVISIBLE : View.VISIBLE);
                    break;
                case 5: showLanguages(); break;
                case 6: showGuide(); break;
                case 7: returnToLauncher(null); break;
                default: break;
            }
        }).show();
    }

    private void showScale() {
        String[] values = {"75%", "100%", "125%", "150%"};
        new AlertDialog.Builder(this).setTitle("Размер кнопок").setItems(values, (dialog, which) -> {
            float scale = new float[]{0.75f, 1.0f, 1.25f, 1.5f}[which];
            settings.controlScale(scale);
            controls.setControlScale(scale);
        }).show();
    }

    private void showAlpha() {
        String[] values = {"25%", "50%", "75%", "100%"};
        new AlertDialog.Builder(this).setTitle("Прозрачность кнопок").setItems(values, (dialog, which) -> {
            float alpha = new float[]{0.25f, 0.5f, 0.75f, 1.0f}[which];
            settings.controlAlpha(alpha);
            controls.setControlAlpha(alpha);
        }).show();
    }

    private void showLanguages() {
        boolean russian = PortSettings.hasRussianData(this);
        String[] values = russian ? new String[]{"日本語", "Русский"} : new String[]{"日本語"};
        new AlertDialog.Builder(this).setTitle("Локализация").setItems(values, (dialog, which) -> {
            returnToLauncher(which == 0 ? "jp" : "ru");
        }).show();
    }

    private void showGuide() {
        new AlertDialog.Builder(this).setTitle("Управление").setMessage(
            "Отдельные кнопки слева: движение, диагонали и прицеливание.\n\n" +
            "Кнопки справа соответствуют раскладке первого игрока и отмечены значками действий.\n\n" +
            "II в левом верхнем углу — пауза. Удержание II — дополнительные настройки.\n\n" +
            "Расположение, размер, отступ, прозрачность и значки настраиваются в игре.").show();
    }

    void returnToLauncher(String language) {
        if (lanDialog != null) { lanDialog.close(); lanDialog = null; }
        if (lanLobby != null) { lanLobby.close(root); lanLobby = null; }
        if (language != null) {
            settings.language(language);
            org.griefsyndrome.port.launcher.State.put(this, org.griefsyndrome.port.launcher.State.CHOICES,
                "option.russian", "ru".equals(language) ? "1" : "0");
        }
        org.griefsyndrome.port.launcher.Launch.openLauncher(this, launch.distribution(), language != null);
        Process.killProcess(Process.myPid());
    }

    void returnToMainMenu() {
        org.griefsyndrome.port.launcher.State.put(this, org.griefsyndrome.port.launcher.State.CHOICES,
            "pending.profile", getIntent().getStringExtra("userdata"));
        returnToLauncher(settings.language());
    }

    private void showLanDialog() {
        if (nativeQaFlags() != 0) {
            new AlertDialog.Builder(this).setTitle("LAN недоступен")
                .setMessage("Выключите все опции QA: разблокировку уровней, бессмертие и One shot kill.")
                .setPositiveButton("OK", null).show();
            return;
        }
        if (!PortSettings.hasRussianData(this)) {
            new AlertDialog.Builder(this).setTitle("LAN-мультиплеер")
                .setMessage("Для русского сетевого лобби нужны проверенные архивы gs02.dat и gs03.dat.")
                .setPositiveButton("OK", null).show();
            return;
        }
        if (Build.VERSION.SDK_INT >= 37 && checkSelfPermission(LAN_PERMISSION) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {LAN_PERMISSION}, LAN_REQUEST);
            return;
        }
        if (lanDialog != null) lanDialog.close();
        lanDialog = LanDialog.show(this, (host, address, port, players) -> {
            settings.lanHost(host);
            settings.lanAddress(address);
            settings.lanPort(port);
            settings.lanPlayers(players);
            LauncherHooks.prepareLan(this, host, address, port, players, getIntent().getStringExtra("userdata"));
            org.griefsyndrome.port.launcher.State.put(this, org.griefsyndrome.port.launcher.State.CHOICES,
                "option.russian", "1");
            returnToLauncher("ru");
        });
    }

    private void handleBack() {
        nativeReleaseAll();
        if (confirmDialog != null) {
            confirmDialog.cancel();
        } else if (controls.isEditing()) {
            controls.setEditing(false);
        } else if (lanLobby != null) {
            returnToMainMenu();
        } else if (!started) {
            returnToLauncher(null);
        } else if (!nativeBack()) {
            tap(0x1B);
        }
    }

    @Override
    public void onBackPressed() {
        handleBack();
    }

    private int gamepadPlayer(KeyEvent event) {
        int id = event.getDeviceId();
        int existing = gamepadPlayers.get(id, -1);
        if (existing >= 0) return existing;
        for (int player = 0; player < 3; ++player) {
            if (gamepadPlayers.indexOfValue(player) < 0) {
                gamepadPlayers.put(id, player);
                return player;
            }
        }
        return 0;
    }

    private int mapKey(int code, KeyEvent event) {
        if (code == KeyEvent.KEYCODE_ENTER) return 'Z';
        if (code == KeyEvent.KEYCODE_ESCAPE) return 0x1B;
        if (inputSource(event) == 2) {
            int player = gamepadPlayer(event);
            if (code >= KeyEvent.KEYCODE_DPAD_UP && code <= KeyEvent.KEYCODE_DPAD_RIGHT)
                return nativePadDirection(code - KeyEvent.KEYCODE_DPAD_UP, player);
            int key = nativePadKey(code, player);
            if (key != 0) return key;
        }
        return nativeAndroidKey(code);
    }

    @Override
    public boolean onKeyDown(int code, KeyEvent event) {
        if (nativeBindingEvent(inputSource(event), code)) return true;
        int key = mapKey(code, event);
        if (key != 0) {
            keyFromSource(key, inputSource(event), true);
            return true;
        }
        return super.onKeyDown(code, event);
    }

    @Override
    public boolean onKeyUp(int code, KeyEvent event) {
        if (nativeBindingEvent(inputSource(event), code)) return true;
        int key = mapKey(code, event);
        if (key != 0) {
            keyFromSource(key, inputSource(event), false);
            return true;
        }
        return super.onKeyUp(code, event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if ((event.getSource() & InputDevice.SOURCE_CLASS_JOYSTICK) != 0 &&
            event.getActionMasked() == MotionEvent.ACTION_MOVE) {
            float hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X);
            float hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y);
            float x = Math.abs(hatX) > 0.35f ? hatX : event.getAxisValue(MotionEvent.AXIS_X);
            float y = Math.abs(hatY) > 0.35f ? hatY : event.getAxisValue(MotionEvent.AXIS_Y);
            int directions = (x < -0.35f ? 4 : 0) | (x > 0.35f ? 8 : 0) |
                (y < -0.35f ? 1 : 0) | (y > 0.35f ? 2 : 0);
            int id = event.getDeviceId();
            int old = directionStates.get(id, 0);
            int player = gamepadPlayers.get(id, 0);
            for (int direction = 0; direction < 4; ++direction) {
                int bit = 1 << direction;
                if ((directions & bit) != (old & bit))
                    keyFromSource(nativePadDirection(direction, player), 2, (directions & bit) != 0);
            }
            directionStates.put(id, directions);
            if (settings.controlsAutoHide() && !controls.isEditing()) controls.setVisibility(View.INVISIBLE);
            return true;
        }
        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    public boolean dispatchTouchEvent(MotionEvent event) {
        if (menuButton.getVisibility() == View.VISIBLE && event.getX() >= menuButton.getLeft() &&
            event.getX() < menuButton.getRight() && event.getY() >= menuButton.getTop() && event.getY() < menuButton.getBottom())
            return super.dispatchTouchEvent(event);
        if (!controls.isEditing() && controls.getVisibility() == View.VISIBLE && controls.handlesTouch(event.getX(), event.getY()))
            return super.dispatchTouchEvent(event);
        if (event.getActionMasked() == MotionEvent.ACTION_POINTER_DOWN) nativeMenuTouch(MotionEvent.ACTION_CANCEL, 0, 0);
        boolean nativeHandled = lanLobby == null && !controls.isEditing() && nativeMenuTouch(event.getActionMasked(),
            event.getX() / controls.getWidth(), event.getY() / controls.getHeight());
        if (nativeHandled) return true;
        if (settings.controlsAutoHide() && event.getActionMasked() == MotionEvent.ACTION_DOWN)
            controls.setVisibility(View.VISIBLE);
        return super.dispatchTouchEvent(event);
    }

    static void onGameExit(int code) {
        GameActivity activity = current;
        if (activity == null) return;
        activity.launch.exiting(code, null);
        if (code != 0) activity.launch.failed("The game stopped: code " + code + ". Log: GriefSyndrome/userdata/griefsyndrome.log");
    }

    private native void nativeStart(String game, String data, String images, String language,
        int width, int height, boolean skipLogo, boolean lan, boolean lanHost, String lanAddress,
        int lanPort, int lanPlayers, boolean verbose);
    private static native void nativeSurface(android.view.Surface surface);
    private static native void nativePause(boolean paused);
    static native void nativeKey(int key, boolean down);
    private static native void nativeKeyFromSource(int key, boolean down, int source);
    static native void nativeReleaseAll();
    private static native void nativeFpsLimit(float value);
    private static native void nativeFpsChoice(float value);
    private static native void nativeFpsRates(float[] values);
    private static native int nativeLoadWork();
    private static native boolean nativeLoadReady();
    private static native boolean nativeMenuTouch(int action, float x, float y);
    private static native int nativeQaFlags();
    private static native void nativeQaSet(int flags);
    private static native void nativeMenuConfirm(int kind, boolean accepted);
    private static native boolean nativeBack();
    private static native boolean nativeBindingEvent(int source, int code);
    private static native void nativeControlLabels(boolean value);
    static native int nativeTouchBinding(int action);
    private static native int nativePadKey(int code, int player);
    private static native int nativePadDirection(int direction, int player);
    private static native int nativeAndroidKey(int code);
    static native int nativeLanControl(int action);
    static native String nativeLanStatus();
    static native boolean nativeLanCanStart();
    static native boolean nativeLanCanConnect();
}
