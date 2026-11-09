package org.griefsyndrome.port;

import android.content.res.ColorStateList;
import android.graphics.Color;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

final class LanLobby {
    private final GameActivity activity;
    private final boolean host;
    private final int port, players;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final LinearLayout panel;
    private final TextView status;
    private final Button start, retry;
    private boolean active = true, queued, connected;
    private boolean wasReady;
    private long attemptedAt;
    private LanDiscovery discovery;
    private FrameLayout container;

    LanLobby(GameActivity activity, PortSettings settings) {
        this.activity = activity;
        host = activity.getIntent().getBooleanExtra("lanHost", settings.lanHost());
        port = activity.getIntent().getIntExtra("lanPort", settings.lanPort());
        players = activity.getIntent().getIntExtra("lanPlayers", settings.lanPlayers());
        panel = new LinearLayout(activity);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setGravity(Gravity.CENTER);
        panel.setPadding(dp(28), dp(24), dp(28), dp(24));
        panel.setBackgroundColor(0xFF101218);
        TextView title = new TextView(activity);
        title.setText(host ? "Хост · LAN-игра" : "Клиент · LAN-игра");
        title.setTextColor(Color.WHITE);
        title.setTextSize(26);
        title.setGravity(Gravity.CENTER);
        panel.addView(title, new LinearLayout.LayoutParams(-1, -2));
        status = new TextView(activity);
        status.setTextColor(0xFFE3E5EE);
        status.setTextSize(19);
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, dp(24), 0, dp(24));
        panel.addView(status, new LinearLayout.LayoutParams(-1, -2));
        start = new Button(activity);
        style(start);
        start.setText("Начать игру");
        start.setEnabled(false);
        start.setVisibility(host ? View.VISIBLE : View.GONE);
        start.setOnClickListener(view -> {
            start.setEnabled(false);
            GameActivity.nativeLanControl(1);
        });
        panel.addView(start, new LinearLayout.LayoutParams(-1, -2));
        retry = new Button(activity);
        style(retry);
        retry.setText("Повторить подключение");
        retry.setVisibility(View.GONE);
        retry.setOnClickListener(view -> {
            if (discovery != null) { discovery.close(); discovery = null; }
            GameActivity.nativeLanControl(2);
            queued = connected = false;
            retry.setVisibility(View.GONE);
        });
        panel.addView(retry, new LinearLayout.LayoutParams(-1, -2));
        Button back = new Button(activity);
        style(back);
        back.setText("В главное меню");
        back.setOnClickListener(view -> activity.returnToMainMenu());
        panel.addView(back, new LinearLayout.LayoutParams(-1, -2));
        status.setText(host ? "Создаём игру…" : "Подключаемся к хосту…");
    }

    void show(FrameLayout root) {
        ScrollView scroll = new ScrollView(activity);
        scroll.setFillViewport(true);
        scroll.addView(panel, new ScrollView.LayoutParams(-1, -2));
        container = scroll;
        root.addView(container, new FrameLayout.LayoutParams(-1, -1));
        handler.postDelayed(refresh, 500);
    }

    void close(FrameLayout root) {
        active = false;
        handler.removeCallbacksAndMessages(null);
        if (discovery != null) discovery.close();
        root.removeView(container);
    }

    private final Runnable refresh = new Runnable() {
        @Override public void run() {
            if (!active) return;
            boolean canConnect = GameActivity.nativeLanCanConnect();
            if (!queued && retry.getVisibility() != View.VISIBLE && canConnect && GameActivity.nativeLanControl(0) == 0) {
                queued = true;
                attemptedAt = SystemClock.elapsedRealtime();
            }
            if (queued && !canConnect) {
                connected = true;
                if (host && discovery == null) {
                    discovery = new LanDiscovery(activity, text -> status.setText(text));
                    discovery.advertise(port, players);
                }
                if (!host) status.setText("Соединение с хостом устанавливается. Игра начнётся, когда хост нажмёт «Начать игру».");
            } else if (queued && canConnect && (connected || SystemClock.elapsedRealtime() - attemptedAt > 2000)) {
                if (discovery != null) { discovery.close(); discovery = null; }
                if (host && connected) {
                    Log.i("GriefSyndrome", "LAN host reopening after disconnected session");
                    queued = connected = wasReady = false;
                    status.setText("Соединение прервано. Снова открываем комнату…");
                } else {
                    if (retry.getVisibility() != View.VISIBLE) Log.w("GriefSyndrome", "LAN connection failed: " + GameActivity.nativeLanStatus());
                    status.setText("Не удалось подключиться. Проверьте, что оба устройства в одной Wi-Fi сети, и повторите подключение.");
                    retry.setVisibility(View.VISIBLE);
                }
            }
            boolean ready = connected && !canConnect && GameActivity.nativeLanCanStart();
            start.setEnabled(ready);
            if (host && ready) status.setText("Игрок подключился. Можно начинать!");
            else if (host && wasReady && !canConnect) status.setText("Клиент отключился. Ожидание повторного подключения…");
            wasReady = ready;
            handler.postDelayed(this, 500);
        }
    };

    private int dp(int value) { return Math.round(value * activity.getResources().getDisplayMetrics().density); }

    private void style(Button button) {
        button.setBackgroundColor(Color.WHITE);
        button.setTextColor(new ColorStateList(new int[][] {{-android.R.attr.state_enabled}, {}},
            new int[] {0xFF8C909C, 0xFF101218}));
    }
}
