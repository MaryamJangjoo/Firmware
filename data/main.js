import { LitElement, html, css } from 'https://unpkg.com/lit@latest?module';

//
// 1. <theme-toggle>
//
class ThemeToggle extends LitElement {
  static styles = css`
    button {
      padding: 0.5em 1em;
      margin-bottom: 1em;
      font-size: 1em;
      background: var(--accent);
      color: white;
      border: none;
      border-radius: 4px;
      cursor: pointer;
    }
  `;
  constructor() {
    super();
    this.themes = ['light', 'dark', 'contrast'];
    this.index = 0;
  }
  toggleTheme() {
    this.index = (this.index + 1) % this.themes.length;
    document.documentElement.dataset.theme = this.themes[this.index];
  }
  render() {
    return html`<button @click=${this.toggleTheme}>Switch Theme</button>`;
  }
}
customElements.define('theme-toggle', ThemeToggle);


//
// 2. <scene-switcher>
//
class SceneSwitcher extends LitElement {
  static styles = css`
    select {
      padding: 0.5em;
      font-size: 1em;
      margin-bottom: 1em;
    }
  `;
  constructor() {
    super();
    this.scenes = ['default', 'morning', 'party', 'away', 'developer'];
  }
  changeScene(e) {
    document.body.dataset.scene = e.target.value;
  }
  render() {
    return html`
      <select @change=${this.changeScene}>
        ${this.scenes.map(scene => html`<option value="${scene}">${scene}</option>`)}
      </select>
    `;
  }
}
customElements.define('scene-switcher', SceneSwitcher);


//
// 3. <smart-narrator>
//
class SmartNarrator extends LitElement {
  static properties = { message: { type: String } };
  static styles = css`
    div {
      font-style: italic;
      margin-bottom: 1em;
      color: var(--text);
    }
  `;
  constructor() {
    super();
    this.message = 'Welcome, Hossein. The house is calm.';
    setInterval(() => {
      const temp = document.querySelector('temp-display')?.temp || 22;
      const level = document.querySelector('audio-visualizer')?.level || 0;
      this.message = `The house breathes quietly. Temperature is ${temp}°C. Audio level is ${level}%.`;
    }, 4000);
  }
  render() {
    return html`<div>${this.message}</div>`;
  }
}
customElements.define('smart-narrator', SmartNarrator);


//
// 4. <light-toggle>
//
class LightToggle extends LitElement {
  static properties = { state: { type: Boolean } };
  static styles = css`
    svg { cursor: pointer; width: 64px; height: 64px; }
    .on use { fill: gold; }
    .off use { fill: #ccc; }
  `;
  constructor() {
    super();
    this.state = false;
  }
  toggle() {
    this.state = !this.state;
    this.dispatchEvent(new CustomEvent('light-changed', { detail: this.state }));
  }
  render() {
    return html`
      <svg @click=${this.toggle} class="${this.state ? 'on' : 'off'}">
        <use href="graphics.svg#icon-fan" />
      </svg>
    `;
  }
}
customElements.define('light-toggle', LightToggle);


//
// 5. <temp-display>
//
class TempDisplay extends LitElement {
  static properties = { temp: { type: Number } };
  static styles = css`
    .temp { font-size: 1.2em; text-align: center; }
  `;
  constructor() {
    super();
    this.temp = 22;
  }
  render() {
    return html`
      <div>
        <svg class="temp"><use href="graphics.svg#icon-temp" /></svg>
        <div>${this.temp}°C</div>
      </div>
    `;
  }
}
customElements.define('temp-display', TempDisplay);


//
// 6. <volume-slider>
//
class VolumeSlider extends LitElement {
  static properties = { volume: { type: Number } };
  static styles = css`
    input[type="range"] { width: 90%; }
    label { display: block; margin-bottom: 0.5em; text-align: center; }
  `;
  constructor() {
    super();
    this.volume = 50;
  }
  updateVolume(e) {
    this.volume = e.target.value;
    this.dispatchEvent(new CustomEvent('volume-changed', { detail: this.volume }));
  }
  render() {
    return html`
      <label>
        <svg><use href="graphics.svg#icon-audio" /></svg>
        Volume: ${this.volume}%
      </label>
      <input type="range" min="0" max="100" .value=${this.volume} @input=${this.updateVolume} />
    `;
  }
}
customElements.define('volume-slider', VolumeSlider);


//
// 7. <audio-visualizer>
//
class AudioVisualizer extends LitElement {
  static properties = { level: { type: Number } };
  static styles = css`
    .bar {
      height: 10px;
      background: #0f0;
      transition: width 0.1s ease;
      margin-top: 0.5em;
    }
  `;
  constructor() {
    super();
    this.level = 0;
  }
  render() {
    return html`
      <svg><use href="graphics.svg#icon-audio" /></svg>
      <div class="bar" style="width: ${this.level}%"></div>
    `;
  }
}
customElements.define('audio-visualizer', AudioVisualizer);


//
// 8. <energy-chart>
//
class EnergyChart extends LitElement {
  static properties = { data: { type: Array } };
  static styles = css`
    svg { width: 100%; height: 100px; }
    polyline { fill: none; stroke: #00f; stroke-width: 2; }
  `;
  constructor() {
    super();
    this.data = [10, 20, 15, 30, 25, 40];
  }
  render() {
    const points = this.data.map((v, i) => `${i * 20},${100 - v}`).join(' ');
    return html`
      <svg viewBox="0 0 120 100">
        <polyline points="${points}" />
      </svg>
    `;
  }
}
customElements.define('energy-chart', EnergyChart);

class SmartMap extends LitElement {
  static styles = css`
    svg { width: 100%; height: auto; }
    .device { fill: var(--accent); cursor: pointer; }
    .device:hover { fill: orange; }
  `;
  render() {
    return html`
      <svg viewBox="0 0 200 100">
        <rect x="10" y="10" width="180" height="80" fill="#eee" stroke="#ccc" />
        <circle class="device" cx="40" cy="40" r="8">
          <animate attributeName="r" values="8;10;8" dur="1s" repeatCount="indefinite"/>
        </circle>
        <circle class="device" cx="160" cy="60" r="8">
          <animate attributeName="r" values="8;12;8" dur="2s" repeatCount="indefinite"/>
        </circle>
      </svg>
    `;
  }
}
customElements.define('smart-map', SmartMap);

class SmartSuggestions extends LitElement {
  static properties = { hint: { type: String } };
  static styles = css`
    div { font-size: 0.95em; color: var(--text); text-align: center; }
  `;
  constructor() {
    super();
    this.hint = 'Would you like to dim the lights at sunset?';
    setInterval(() => {
      const hints = [
        'Schedule fan to turn off at midnight?',
        'Auto-lock doors when away mode is active?',
        'Dim lights when audio level is high?',
        'Turn on garden irrigation at 6 AM?'
      ];
      this.hint = hints[Math.floor(Math.random() * hints.length)];
    }, 6000);
  }
  render() {
    return html`<div>💡 ${this.hint}</div>`;
  }
}
customElements.define('smart-suggestions', SmartSuggestions);

class SystemHealth extends LitElement {
  static properties = { status: { type: String } };
  static styles = css`
    .pulse { stroke: #e00; stroke-width: 2; fill: none; }
    .text { margin-top: 0.5em; text-align: center; font-size: 0.9em; }
  `;
  constructor() {
    super();
    this.status = 'All systems stable.';
  }
  render() {
    return html`
      <svg viewBox="0 0 64 64">
        <polyline class="pulse" points="0,32 10,32 15,20 20,44 25,32 30,32 35,18 40,46 45,32 64,32">
          <animate attributeName="stroke" values="#e00;#f66;#e00" dur="1s" repeatCount="indefinite"/>
        </polyline>
      </svg>
      <div class="text">${this.status}</div>
    `;
  }
}
customElements.define('system-health', SystemHealth);
class DevPanel extends LitElement {
  static properties = { debug: { type: String } };
  static styles = css`
    pre { font-size: 0.8em; background: #222; color: #0f0; padding: 0.5em; border-radius: 4px; overflow-x: auto; }
  `;
  constructor() {
    super();
    this.debug = '{}';
    setInterval(() => {
      const temp = document.querySelector('temp-display')?.temp || 0;
      const volume = document.querySelector('volume-slider')?.volume || 0;
      const level = document.querySelector('audio-visualizer')?.level || 0;
      this.debug = JSON.stringify({ temp, volume, level }, null, 2);
    }, 2000);
  }
  render() {
    return html`<pre>${this.debug}</pre>`;
  }
}
customElements.define('dev-panel', DevPanel);

setInterval(() => {
  document.querySelector('temp-display').temp = 18 + Math.floor(Math.random() * 15);
}, 3000);

setInterval(() => {
  document.querySelector('volume-slider').volume = Math.floor(Math.random() * 101);
}, 4000);

setInterval(() => {
  document.querySelector('audio-visualizer').level = Math.floor(Math.random() * 101);
}, 200);

setInterval(() => {
  const chart = document.querySelector('energy-chart');
  chart.data = Array.from({ length: 6 }, () => Math.floor(Math.random() * 50) + 10);
}, 5000);
