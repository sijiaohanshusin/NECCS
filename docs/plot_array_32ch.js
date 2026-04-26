(function () {
  function computePackageCenter(mic, options) {
    const offsetX = options && Number.isFinite(options.offsetX) ? options.offsetX : 0.68;
    const offsetY = options && Number.isFinite(options.offsetY) ? options.offsetY : 0.0;
    const globalDelta = options && Number.isFinite(options.globalDelta) ? options.globalDelta : 0.0;
    const rotation = (options && options.rotationByMic && Number.isFinite(options.rotationByMic[mic.id]))
      ? options.rotationByMic[mic.id]
      : (mic.rotation_deg || 0);
    const theta = (rotation + globalDelta) * Math.PI / 180;
    const rotX = offsetX * Math.cos(theta) - offsetY * Math.sin(theta);
    const rotY = offsetX * Math.sin(theta) + offsetY * Math.cos(theta);
    return {
      x_mm: mic.acoustic_x_mm - rotX,
      y_mm: mic.acoustic_y_mm - rotY,
      rotation_deg: rotation + globalDelta
    };
  }

  function renderArray(canvasOrId, scheme, options) {
    const canvas = typeof canvasOrId === "string" ? document.getElementById(canvasOrId) : canvasOrId;
    if (!canvas || !scheme) return;
    const ctx = canvas.getContext("2d");
    const boardHalf = scheme.board_half_mm || 50;
    const keepoutHalf = scheme.keepout_half_mm || 14;
    const width = canvas.width;
    const height = canvas.height;
    const scale = Math.min(width, height) / (2 * boardHalf + 12);
    const px = (x) => width / 2 + x * scale;
    const py = (y) => height / 2 - y * scale;
    const viewMode = (options && options.viewMode) || "package";

    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = "#fbfdff";
    ctx.fillRect(0, 0, width, height);
    ctx.strokeStyle = "#d1d9e6";
    ctx.lineWidth = 1;
    ctx.strokeRect(px(-boardHalf), py(boardHalf), 2 * boardHalf * scale, 2 * boardHalf * scale);
    ctx.strokeStyle = "#dc2626";
    ctx.strokeRect(px(-keepoutHalf), py(keepoutHalf), 2 * keepoutHalf * scale, 2 * keepoutHalf * scale);

    scheme.mics.forEach((mic) => {
      const acousticX = mic.acoustic_x_mm;
      const acousticY = mic.acoustic_y_mm;
      const pkg = computePackageCenter(mic, options || {});
      const color = mic.layer === "Outer16" ? "#2563eb" : (mic.layer === "Transition8" ? "#f59e0b" : "#0f766e");

      if (viewMode === "package") {
        ctx.strokeStyle = "#94a3b8";
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(px(pkg.x_mm), py(pkg.y_mm));
        ctx.lineTo(px(acousticX), py(acousticY));
        ctx.stroke();

        ctx.fillStyle = "#ffffff";
        ctx.strokeStyle = color;
        ctx.lineWidth = 1.6;
        ctx.fillRect(px(pkg.x_mm) - 4, py(pkg.y_mm) - 4, 8, 8);
        ctx.strokeRect(px(pkg.x_mm) - 4, py(pkg.y_mm) - 4, 8, 8);

        ctx.beginPath();
        ctx.fillStyle = color;
        ctx.arc(px(acousticX), py(acousticY), 2.5, 0, Math.PI * 2);
        ctx.fill();
      } else {
        ctx.beginPath();
        ctx.fillStyle = color;
        ctx.strokeStyle = "#0f172a";
        ctx.lineWidth = 1;
        ctx.arc(px(acousticX), py(acousticY), mic.core16 ? 5.2 : 4.8, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();
      }

      ctx.font = "10px 'Segoe UI', sans-serif";
      ctx.fillStyle = "#0f172a";
      const labelX = viewMode === "package" ? pkg.x_mm : acousticX;
      const labelY = viewMode === "package" ? pkg.y_mm : acousticY;
      ctx.fillText(mic.id, px(labelX) + 6, py(labelY) - 6);
    });

    ctx.font = "bold 13px 'Segoe UI', sans-serif";
    ctx.fillStyle = "#0f172a";
    ctx.fillText((options && options.title) || scheme.title || "", 12, 18);
  }

  if (typeof window !== "undefined") {
    window.NECCSArrayPlot = { computePackageCenter, renderArray };
  }
})();
