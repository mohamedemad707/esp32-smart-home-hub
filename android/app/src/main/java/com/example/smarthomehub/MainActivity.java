package com.example.smarthomehub;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.google.android.material.switchmaterial.SwitchMaterial;
import com.google.firebase.auth.FirebaseAuth;
import com.google.firebase.auth.FirebaseUser;
import com.google.firebase.database.DataSnapshot;
import com.google.firebase.database.DatabaseError;
import com.google.firebase.database.DatabaseReference;
import com.google.firebase.database.FirebaseDatabase;
import com.google.firebase.database.ValueEventListener;
import com.google.firebase.messaging.FirebaseMessaging;

import java.text.DateFormat;
import java.util.Date;
import java.util.Locale;

public class MainActivity extends AppCompatActivity {

    private static final String TOPIC_ALERTS = "alerts";

    private FirebaseAuth auth;
    private DatabaseReference hubRef;
    private ValueEventListener hubListener;

    private LinearLayout loginGroup, controlGroup;
    private EditText emailInput, passwordInput;
    private TextView tempText, humText, updatedText, relayStateText, statusText;
    private SwitchMaterial relaySwitch;

    // Prevents sending a command back to Firebase when we update the switch from the server
    private boolean updatingFromServer = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        auth = FirebaseAuth.getInstance();
        hubRef = FirebaseDatabase.getInstance().getReference("hub");

        loginGroup = findViewById(R.id.loginGroup);
        controlGroup = findViewById(R.id.controlGroup);
        emailInput = findViewById(R.id.emailInput);
        passwordInput = findViewById(R.id.passwordInput);
        tempText = findViewById(R.id.tempText);
        humText = findViewById(R.id.humText);
        updatedText = findViewById(R.id.updatedText);
        relayStateText = findViewById(R.id.relayStateText);
        statusText = findViewById(R.id.statusText);
        relaySwitch = findViewById(R.id.relaySwitch);

        Button loginButton = findViewById(R.id.loginButton);
        Button logoutButton = findViewById(R.id.logoutButton);

        loginButton.setOnClickListener(v -> signIn());
        logoutButton.setOnClickListener(v -> {
            stopListening();
            FirebaseMessaging.getInstance().unsubscribeFromTopic(TOPIC_ALERTS);
            auth.signOut();
            showLogin();
        });

        relaySwitch.setOnCheckedChangeListener((buttonView, isChecked) -> {
            if (updatingFromServer) return;
            hubRef.child("relay").setValue(isChecked)
                    .addOnFailureListener(e -> Toast.makeText(this,
                            getString(R.string.command_failed, e.getMessage()),
                            Toast.LENGTH_SHORT).show());
        });

        requestNotificationPermission();
    }

    @Override
    protected void onStart() {
        super.onStart();
        if (auth.getCurrentUser() != null) {
            showControl();
        } else {
            showLogin();
        }
    }

    @Override
    protected void onStop() {
        super.onStop();
        stopListening();
    }

    // ---------- Auth ----------
    private void signIn() {
        String email = emailInput.getText().toString().trim();
        String password = passwordInput.getText().toString();
        if (email.isEmpty() || password.isEmpty()) {
            Toast.makeText(this, R.string.enter_credentials, Toast.LENGTH_SHORT).show();
            return;
        }
        auth.signInWithEmailAndPassword(email, password)
                .addOnSuccessListener(result -> showControl())
                .addOnFailureListener(e -> Toast.makeText(this,
                        getString(R.string.login_failed, e.getMessage()),
                        Toast.LENGTH_LONG).show());
    }

    private void showLogin() {
        loginGroup.setVisibility(View.VISIBLE);
        controlGroup.setVisibility(View.GONE);
    }

    private void showControl() {
        FirebaseUser user = auth.getCurrentUser();
        if (user == null) {
            showLogin();
            return;
        }
        loginGroup.setVisibility(View.GONE);
        controlGroup.setVisibility(View.VISIBLE);
        FirebaseMessaging.getInstance().subscribeToTopic(TOPIC_ALERTS);
        startListening();
    }

    // ---------- Realtime Database ----------
    private void startListening() {
        if (hubListener != null) return;
        hubListener = new ValueEventListener() {
            @Override
            public void onDataChange(@NonNull DataSnapshot snapshot) {
                Double temp = snapshot.child("temperature").getValue(Double.class);
                Double hum = snapshot.child("humidity").getValue(Double.class);
                Long updatedAt = snapshot.child("updatedAt").getValue(Long.class);
                Boolean relayCmd = snapshot.child("relay").getValue(Boolean.class);
                Boolean relayState = snapshot.child("relayState").getValue(Boolean.class);

                tempText.setText(temp == null ? "--"
                        : String.format(Locale.US, "%.1f °C", temp));
                humText.setText(hum == null ? "--"
                        : String.format(Locale.US, "%.1f %%", hum));
                updatedText.setText(updatedAt == null
                        ? getString(R.string.last_update, "--")
                        : getString(R.string.last_update,
                                DateFormat.getTimeInstance().format(new Date(updatedAt))));

                String lampState = relayState == null ? "--"
                        : getString(relayState ? R.string.on : R.string.off);
                relayStateText.setText(getString(R.string.lamp_state, lampState));

                updatingFromServer = true;
                relaySwitch.setChecked(relayCmd != null && relayCmd);
                updatingFromServer = false;

                statusText.setText(R.string.connected);
            }

            @Override
            public void onCancelled(@NonNull DatabaseError error) {
                statusText.setText(getString(R.string.error_prefix, error.getMessage()));
            }
        };
        hubRef.addValueEventListener(hubListener);
    }

    private void stopListening() {
        if (hubListener != null) {
            hubRef.removeEventListener(hubListener);
            hubListener = null;
        }
    }

    // ---------- Notifications (Android 13+) ----------
    private void requestNotificationPermission() {
        if (Build.VERSION.SDK_INT >= 33
                && ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this,
                    new String[]{Manifest.permission.POST_NOTIFICATIONS}, 100);
        }
    }
}
