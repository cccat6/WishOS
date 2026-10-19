// Measured progress and confirmed completion are separate inputs.
export function createWishLoader(element, { onState = () => {} } = {}) {
  const progressPath = element.querySelector('.wish-progress');
  const sparkle = element.querySelector('.wish-spark');
  const motion = matchMedia('(prefers-reduced-motion: reduce)');
  let state = 'loading';
  let progress = 0;
  let destroyed = false;
  function notify() { onState({state,progress}); }
  function setState(next) {
    state = next;
    element.dataset.state = next;
    const loading = next === 'loading';
    element.setAttribute('role',loading?'progressbar':'img');
    if (loading) {
      element.setAttribute('aria-valuemin','0');
      element.setAttribute('aria-valuemax','100');
      element.setAttribute('aria-valuenow',String(progress));
      element.setAttribute('aria-label',element.dataset.loading || 'Loading');
    } else {
      for (const name of ['aria-valuemin','aria-valuemax','aria-valuenow']) element.removeAttribute(name);
      element.setAttribute('aria-label', next === 'error' ? element.dataset.failed || 'Operation failed' : element.dataset.done || 'Complete');
    }
    notify();
  }
  function finish() { if (state === 'completing') setState('complete'); }
  function animationEnd(event) { if (event.target === sparkle && event.animationName === 'wish-pop') finish(); }
  function motionChange() { if (motion.matches) finish(); }
  sparkle.addEventListener('animationend',animationEnd);
  motion.addEventListener('change',motionChange);
  const api = {
    get state() { return state; },
    setProgress(value) {
      if (destroyed || state !== 'loading') return false;
      if (typeof value !== 'number' || !Number.isFinite(value) || value < 0 || value > 100) throw new RangeError('Progress must be a finite number from 0 to 100');
      progress = value;
      progressPath.style.strokeDashoffset = String(100-value);
      progressPath.style.visibility = value === 0 ? 'hidden' : 'visible';
      element.setAttribute('aria-valuenow',String(value));
      notify();
      return true;
    },
    complete() {
      if (destroyed || state !== 'loading') return false;
      progress = 100;
      progressPath.style.strokeDashoffset = '0';
      progressPath.style.visibility = 'visible';
      setState('completing');
      if (motion.matches) finish();
      return true;
    },
    fail() {
      if (destroyed || !['loading','completing'].includes(state)) return false;
      setState('error');
      return true;
    },
    reset() {
      if (destroyed) return;
      progress = 0;
      progressPath.style.strokeDashoffset = '100';
      progressPath.style.visibility = 'hidden';
      setState('loading');
    },
    destroy() {
      destroyed = true;
      sparkle.removeEventListener('animationend',animationEnd);
      motion.removeEventListener('change',motionChange);
    }
  };
  api.reset();
  return api;
}
