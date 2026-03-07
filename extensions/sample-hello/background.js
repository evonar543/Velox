chrome.runtime.onInstalled.addListener(() => {
  console.log("Velox Sample Hello installed");
});

chrome.action.onClicked.addListener(async (tab) => {
  if (!tab || tab.id === undefined) {
    return;
  }

  await chrome.tabs.sendMessage(tab.id, { type: "velox-sample-hello" }).catch(() => {});
});
