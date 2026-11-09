package org.griefsyndrome.port;

import android.content.Context;
import android.content.Intent;
import java.io.File;
import java.util.Collections;
import java.util.List;
import java.util.HashMap;
import java.util.Map;
import org.griefsyndrome.port.launcher.Launch;
import org.griefsyndrome.port.launcher.PortHooks;
import org.griefsyndrome.port.launcher.State;

public final class LauncherHooks implements PortHooks {
    static void prepareLan(Context context, boolean host, String address, int port, int players, String profile) {
        State.put(context, State.CHOICES, "pending.lan", "1");
        State.put(context, State.CHOICES, "pending.lanHost", host ? "1" : "0");
        State.put(context, State.CHOICES, "pending.lanAddress", address);
        State.put(context, State.CHOICES, "pending.lanPort", Integer.toString(port));
        State.put(context, State.CHOICES, "pending.lanPlayers", Integer.toString(players));
        State.put(context, State.CHOICES, "pending.profile", profile);
    }

    @Override
    public void migrate(Context context) {
        PortSettings settings = new PortSettings(context);
        Map<String, String> choices = new HashMap<>();
        choices.put("intro.grief", settings.skipLogo() ? "1" : "0");
        choices.put("option.russian", "ru".equals(settings.language()) ? "1" : "0");
        Launch.adopt(context, choices);
    }

    @Override
    public List<String> missing(Context context, String distribution, String part, File folder) {
        String problem = PortSettings.gameDataProblem(folder);
        return problem == null ? Collections.emptyList() : Collections.singletonList(problem);
    }

    @Override
    public void prepare(Context context, String distribution, Intent intent) {
        boolean russian = PortSettings.hasRussianData(context) &&
            "1".equals(State.get(context, State.CHOICES, "option.russian", "1"));
        intent.putExtra("language", russian ? "ru" : "jp");
        if ("1".equals(State.get(context, State.CHOICES, "pending.lan", "0"))) {
            String profile = State.get(context, State.CHOICES, "pending.profile", "");
            if (new PortSettings(context, profile).qaFlags() != 0)
                throw new IllegalStateException("Disable every QA option before using LAN");
            intent.putExtra("lan", true);
            intent.putExtra("lanHost", "1".equals(State.get(context, State.CHOICES, "pending.lanHost", "1")));
            intent.putExtra("lanAddress", State.get(context, State.CHOICES, "pending.lanAddress", ""));
            intent.putExtra("lanPort", Integer.parseInt(State.get(context, State.CHOICES, "pending.lanPort", "6112")));
            intent.putExtra("lanPlayers", Integer.parseInt(State.get(context, State.CHOICES, "pending.lanPlayers", "2")));
            State.put(context, State.CHOICES, "pending.lan", null);
        }
        String profile = State.get(context, State.CHOICES, "pending.profile", "");
        if (!profile.isEmpty()) intent.putExtra("userdata", profile);
        State.put(context, State.CHOICES, "pending.profile", null);
    }
}
