(() => {
  const applyMarker = () => {
    if (document.documentElement) {
      document.documentElement.setAttribute("data-velox-sample-extension", "active");
    }
  };

  applyMarker();
  chrome.runtime.onMessage.addListener((message) => {
    if (message && message.type === "velox-sample-hello") {
      applyMarker();
      console.log("Velox Sample Hello pinged this page");
    }
  });
})();
