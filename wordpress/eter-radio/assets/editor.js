/* Éter Radio: the three blocks in the block editor (server-side rendered preview with a settings panel). */
(function (wp) {
    'use strict';
    var el = wp.element.createElement;
    var Fragment = wp.element.Fragment;
    var ServerSideRender = wp.serverSideRender;
    var InspectorControls = wp.blockEditor.InspectorControls;
    var useBlockProps = wp.blockEditor.useBlockProps;
    var C = wp.components;
    var L = window.eterRadioEditor || {};

    function icon(path) {
        return el('svg', { viewBox: '0 0 24 24', xmlns: 'http://www.w3.org/2000/svg' }, el('path', { d: path }));
    }
    var icons = {
        now: icon('M12 3v10.55A4 4 0 1 0 14 17V7h4V3h-6z'),
        next: icon('M3 6h12v2H3zm0 5h12v2H3zm0 5h8v2H3zm14-6v8l5-4z'),
        top: icon('M7 21h10v-2h-4v-3.1A5 5 0 0 0 17 11V4h2V2H5v2h2v7a5 5 0 0 0 4 4.9V19H7v2zm2-10V4h6v7a3 3 0 0 1-6 0z'),
        request: icon('M15.5 14h-.79l-.28-.27A6.47 6.47 0 0 0 16 9.5 6.5 6.5 0 1 0 9.5 16c1.61 0 3.09-.59 4.23-1.57l.27.28v.79l5 4.99L20.49 19l-4.99-5zm-6 0C7.01 14 5 11.99 5 9.5S7.01 5 9.5 5 14 7.01 14 9.5 11.99 14 9.5 14z')
    };

    function block(name, title, description, iconEl, controls) {
        wp.blocks.registerBlockType(name, {
            apiVersion: 2,
            title: title,
            description: description,
            icon: iconEl,
            category: 'widgets',
            keywords: ['radio', 'eter', 'éter'],
            supports: { html: false },
            edit: function (props) {
                var a = props.attributes, set = props.setAttributes;
                return el(Fragment, null,
                    el(InspectorControls, null,
                        el(C.PanelBody, { title: title, initialOpen: true },
                            el(C.TextControl, { label: L.heading, value: a.heading, onChange: function (v) { set({ heading: v }); } }),
                            controls ? controls(a, set) : null)),
                    el('div', useBlockProps(), el(ServerSideRender, { block: name, attributes: a })));
            },
            save: function () { return null; }
        });
    }

    block('eter/now-playing', L.now, L.nowDesc, icons.now, function (a, set) {
        return el(C.ToggleControl, { label: L.showProgress, checked: a.showProgress, onChange: function (v) { set({ showProgress: v }); } });
    });
    block('eter/up-next', L.next, L.nextDesc, icons.next, function (a, set) {
        return el(Fragment, null,
            el(C.RangeControl, { label: L.count, value: a.count, min: 1, max: 20, onChange: function (v) { set({ count: v }); } }),
            el(C.ToggleControl, { label: L.showTimes, checked: a.showTimes, onChange: function (v) { set({ showTimes: v }); } }));
    });
    block('eter/most-played', L.top, L.topDesc, icons.top, function (a, set) {
        return el(Fragment, null,
            el(C.SelectControl, { label: L.period, value: a.period, options: [
                { label: L.day, value: 'day' }, { label: L.week, value: 'week' }, { label: L.month, value: 'month' }],
                onChange: function (v) { set({ period: v }); } }),
            el(C.RangeControl, { label: L.count, value: a.count, min: 1, max: 20, onChange: function (v) { set({ count: v }); } }),
            el(C.ToggleControl, { label: L.showPlays, checked: a.showPlays, onChange: function (v) { set({ showPlays: v }); } }));
    });
    block('eter/request', L.request, L.requestDesc, icons.request, null);
})(window.wp);
