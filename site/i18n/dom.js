import {translate} from './runtime.js';

// Attribute translation is kept beside text translation, not in each component.
export function applyTranslations(root=document) {
  for(const element of root.querySelectorAll('[data-i18n]'))element.textContent=translate(element.dataset.i18n);
  for(const [marker,attribute] of [['data-i18n-aria','aria-label'],['data-i18n-alt','alt'],['data-i18n-placeholder','placeholder']]) {
    for(const element of root.querySelectorAll(`[${marker}]`))element.setAttribute(attribute,translate(element.getAttribute(marker)));
  }
}
