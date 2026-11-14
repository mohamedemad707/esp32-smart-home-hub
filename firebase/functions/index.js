/**
 * Cloud Function: sends a push notification (FCM topic "alerts") when the
 * temperature rises above the threshold stored at /config/tempThreshold
 * (default 30 °C). It only fires when the reading CROSSES the threshold,
 * so you don't get a notification every minute while it stays hot.
 *
 * Deploy:  firebase deploy --only functions
 * Note: Cloud Functions require the Blaze (pay-as-you-go) plan; usage for a
 * single device stays inside the free quota.
 */
const { onValueWritten } = require("firebase-functions/v2/database");
const { initializeApp } = require("firebase-admin/app");
const { getMessaging } = require("firebase-admin/messaging");
const { getDatabase } = require("firebase-admin/database");

initializeApp();

const DEFAULT_THRESHOLD = 30;

exports.temperatureAlert = onValueWritten(
  {
    ref: "/hub/temperature",
    // Must match your database region, e.g. "europe-west1" or "us-central1"
    region: "us-central1",
  },
  async (event) => {
    const before = event.data.before.val();
    const after = event.data.after.val();
    if (typeof after !== "number") return;

    const snap = await getDatabase().ref("/config/tempThreshold").get();
    const threshold = typeof snap.val() === "number" ? snap.val() : DEFAULT_THRESHOLD;

    const wasBelow = typeof before !== "number" || before <= threshold;
    if (!(wasBelow && after > threshold)) return;

    await getMessaging().send({
      topic: "alerts",
      notification: {
        title: "High temperature",
        body: `Temperature is ${after.toFixed(1)} °C (limit ${threshold} °C).`,
      },
      android: { priority: "high" },
    });
  }
);
