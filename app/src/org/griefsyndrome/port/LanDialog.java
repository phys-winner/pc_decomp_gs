package org.griefsyndrome.port;

import android.app.Activity;
import android.app.AlertDialog;
import android.net.nsd.NsdServiceInfo;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;
import java.util.ArrayList;

final class LanDialog implements AutoCloseable {
    interface Listener { void connect(boolean host, String address, int port, int players); }

    private final Activity activity;
    private final Listener listener;
    private AlertDialog dialog;
    private LanDiscovery discovery;
    private boolean closed;

    private LanDialog(Activity activity, Listener listener) {
        this.activity = activity;
        this.listener = listener;
    }

    static LanDialog show(Activity activity, Listener listener) {
        LanDialog flow = new LanDialog(activity, listener);
        LinearLayout panel = flow.panel();
        panel.addView(flow.text("Подключите устройства к одной Wi-Fi сети. На одном создайте игру, на остальных найдите её."));
        Button host = new Button(activity);
        host.setText("Создать игру · Хост");
        flow.style(host);
        host.setOnClickListener(view -> {
            flow.close();
            listener.connect(true, "127.0.0.1", LanDiscovery.PORT, 3);
        });
        panel.addView(host, new LinearLayout.LayoutParams(-1, -2));
        Button client = new Button(activity);
        client.setText("Найти игру · Клиент");
        flow.style(client);
        client.setOnClickListener(view -> flow.search());
        panel.addView(client, new LinearLayout.LayoutParams(-1, -2));
        AlertDialog roles = new AlertDialog.Builder(activity).setTitle("LAN-мультиплеер")
            .setView(panel).setNegativeButton("В главное меню", null).create();
        flow.dialog = roles;
        roles.setOnDismissListener(d -> { if (flow.dialog == roles) flow.close(); });
        roles.show();
        return flow;
    }

    private void search() {
        AlertDialog roles = dialog;
        LinearLayout panel = panel();
        TextView status = text("Поиск комнат…");
        panel.addView(status);
        FrameLayout roomsPanel = new FrameLayout(activity);
        ListView list = new ListView(activity);
        ArrayList<NsdServiceInfo> rooms = new ArrayList<>();
        ArrayAdapter<String> adapter = new ArrayAdapter<String>(activity, android.R.layout.simple_list_item_1) {
            @Override public View getView(int position, View recycled, ViewGroup parent) {
                TextView row = (TextView) super.getView(position, recycled, parent);
                row.setTextColor(0xFFE3E5EE);
                return row;
            }
        };
        list.setBackgroundColor(0xFF1E222C);
        list.setAdapter(adapter);
        roomsPanel.addView(list, new FrameLayout.LayoutParams(-1, -1));
        TextView empty = text("Пока нет доступных комнат.\nНа другом устройстве нажмите «Создать игру · Хост». Список обновится автоматически.");
        empty.setGravity(Gravity.CENTER);
        roomsPanel.addView(empty, new FrameLayout.LayoutParams(-1, -1));
        list.setEmptyView(empty);
        int screenDp = Math.round(activity.getResources().getDisplayMetrics().heightPixels /
            activity.getResources().getDisplayMetrics().density);
        panel.addView(roomsPanel, new LinearLayout.LayoutParams(-1, dp(Math.min(240, Math.max(100, screenDp - 240)))));
        list.setOnItemClickListener((parent, view, position, id) -> {
            NsdServiceInfo room = rooms.get(position);
            close();
            listener.connect(false, room.getHost().getHostAddress(), room.getPort(), LanDiscovery.players(room));
        });
        dialog = new AlertDialog.Builder(activity).setTitle("LAN · Выберите комнату")
            .setView(panel).setNegativeButton("В главное меню", null).create();
        dialog.setOnDismissListener(d -> close());
        roles.dismiss();
        dialog.show();
        discovery = new LanDiscovery(activity, new LanDiscovery.Listener() {
            private void update() {
                adapter.clear();
                for (NsdServiceInfo room : rooms)
                    adapter.add(room.getServiceName() + " · до " + LanDiscovery.players(room) + " игроков");
                status.setText("Найдено комнат: " + rooms.size() + ". Нажмите на комнату, чтобы подключиться.");
            }
            @Override public void status(String text) { if (!closed) status.setText(text); }
            @Override public void found(NsdServiceInfo room) {
                if (closed) return;
                rooms.removeIf(item -> item.getServiceName().equals(room.getServiceName()));
                rooms.add(room);
                update();
            }
            @Override public void lost(String name) {
                if (!closed && rooms.removeIf(item -> item.getServiceName().equals(name))) update();
            }
        });
        discovery.search();
    }

    private LinearLayout panel() {
        LinearLayout panel = new LinearLayout(activity);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setPadding(dp(24), dp(8), dp(24), dp(8));
        panel.setBackgroundColor(0xFF101218);
        return panel;
    }

    private TextView text(String value) {
        TextView text = new TextView(activity);
        text.setText(value);
        text.setTextSize(16);
        text.setTextColor(0xFFE3E5EE);
        text.setPadding(0, dp(8), 0, dp(8));
        return text;
    }

    private int dp(int value) { return Math.round(value * activity.getResources().getDisplayMetrics().density); }

    private void style(Button button) {
        button.setTextColor(0xFF101218);
        button.setBackgroundColor(0xFFFFFFFF);
    }

    @Override public void close() {
        if (closed) return;
        closed = true;
        if (discovery != null) discovery.close();
        if (dialog != null) dialog.dismiss();
    }
}
