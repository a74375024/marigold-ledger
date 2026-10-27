package org.marigold.app;

import android.app.AlertDialog;
import android.app.Dialog;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;

/**
 * The system keyboard for a text field (search, accounts): a small dialog
 * with one line, the remote's on-screen keyboard under it. Like the LG
 * keyboard on webOS, it sends the whole value to native as it changes, and
 * once more when it closes (confirmed with OK or Done).
 */
final class Keyboard {
  private Keyboard() {}

  private static Dialog dialog;
  private static boolean sent;

  static void open(MarigoldActivity activity, String value) {
    close();
    EditText field = new EditText(activity);
    field.setSingleLine(true);
    field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
    field.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
    field.setText(value);
    field.setSelection(field.getText().length());
    field.addTextChangedListener(new TextWatcher() {
      @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
      @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
      @Override
      public void afterTextChanged(Editable s) {
        if (!sent) Bridge.nativeText(Bridge.utf8(s.toString()), false, false);
      }
    });
    sent = false;
    AlertDialog d = new AlertDialog.Builder(activity, android.R.style.Theme_DeviceDefault_Dialog_Alert)
        .setView(field)
        .setPositiveButton(R.string.keyboard_ok, (di, which) -> done(field, true))
        .setNegativeButton(R.string.keyboard_cancel, (di, which) -> done(field, false))
        .setOnCancelListener(di -> done(field, false))
        .create();
    field.setOnEditorActionListener((v, actionId, event) -> {
      done(field, true);
      d.dismiss();
      return true;
    });
    if (d.getWindow() != null) d.getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE);
    dialog = d;
    d.show();
    field.requestFocus();
  }

  static void close() {
    Dialog d = dialog;
    dialog = null;
    if (d != null && d.isShowing()) d.dismiss();
  }

  private static void done(EditText field, boolean confirmed) {
    if (sent) return;
    sent = true;
    dialog = null;
    Bridge.nativeText(Bridge.utf8(field.getText().toString()), true, confirmed);
  }
}
