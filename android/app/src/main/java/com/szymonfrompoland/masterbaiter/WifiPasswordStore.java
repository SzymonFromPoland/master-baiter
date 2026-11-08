package com.szymonfrompoland.masterbaiter;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Base64;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.Key;
import java.security.KeyStore;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;

final class WifiPasswordStore {
    private static final String PREFERENCES = "robot_wifi_credentials";
    private static final String PASSWORD_KEY = "encrypted_password";
    private static final String KEYSTORE_ALIAS = "robot_wifi_password_key";
    private static final String KEYSTORE_PROVIDER = "AndroidKeyStore";
    private static final int IV_LENGTH_BYTES = 12;

    private WifiPasswordStore() {
    }

    static String load(Context context) throws GeneralSecurityException, IOException {
        String encoded = preferences(context).getString(PASSWORD_KEY, null);
        if (encoded == null) return null;

        final byte[] payload;
        try {
            payload = Base64.decode(encoded, Base64.NO_WRAP);
        } catch (IllegalArgumentException exception) {
            throw new IOException("Saved Wi-Fi password data is invalid.", exception);
        }
        if (payload.length <= IV_LENGTH_BYTES) {
            throw new IOException("Saved Wi-Fi password data is incomplete.");
        }

        byte[] iv = new byte[IV_LENGTH_BYTES];
        byte[] encrypted = new byte[payload.length - IV_LENGTH_BYTES];
        ByteBuffer.wrap(payload).get(iv).get(encrypted);
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.DECRYPT_MODE, getOrCreateKey(), new GCMParameterSpec(128, iv));
        return new String(cipher.doFinal(encrypted), StandardCharsets.UTF_8);
    }

    static void save(Context context, String password) throws GeneralSecurityException, IOException {
        Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
        cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKey());
        byte[] iv = cipher.getIV();
        if (iv == null || iv.length != IV_LENGTH_BYTES) {
            throw new GeneralSecurityException("Android returned an invalid encryption IV.");
        }

        byte[] encrypted = cipher.doFinal(password.getBytes(StandardCharsets.UTF_8));
        byte[] payload = ByteBuffer.allocate(iv.length + encrypted.length)
                .put(iv)
                .put(encrypted)
                .array();
        String encoded = Base64.encodeToString(payload, Base64.NO_WRAP);
        if (!preferences(context).edit().putString(PASSWORD_KEY, encoded).commit()) {
            throw new IOException("Could not save the encrypted Wi-Fi password.");
        }
    }

    static void clear(Context context) throws IOException {
        if (!preferences(context).edit().remove(PASSWORD_KEY).commit()) {
            throw new IOException("Could not clear the saved Wi-Fi password.");
        }
    }

    private static SharedPreferences preferences(Context context) {
        return context.getApplicationContext()
                .getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE);
    }

    private static SecretKey getOrCreateKey() throws GeneralSecurityException, IOException {
        KeyStore keyStore = KeyStore.getInstance(KEYSTORE_PROVIDER);
        keyStore.load(null);
        Key existing = keyStore.getKey(KEYSTORE_ALIAS, null);
        if (existing != null) {
            if (existing instanceof SecretKey) return (SecretKey) existing;
            throw new GeneralSecurityException("Stored Wi-Fi encryption key has an invalid type.");
        }

        KeyGenerator generator = KeyGenerator.getInstance(
                KeyProperties.KEY_ALGORITHM_AES, KEYSTORE_PROVIDER);
        generator.init(new KeyGenParameterSpec.Builder(
                KEYSTORE_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setRandomizedEncryptionRequired(true)
                .build());
        return generator.generateKey();
    }
}
