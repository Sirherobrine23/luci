'use strict';
'require view';
'require poll';
'require request';
'require rpc';
'require fs';
'require ui';

const callLuciRealtimeStats = rpc.declare({
	object: 'luci',
	method: 'getRealtimeStats',
	params: [ 'mode', 'device' ],
	expect: { result: [] }
});

const pollInterval = 3;

function formatFrequency(value) {
	return value == null ? '-' : '%.2f MHz'.format(value / 1000);
}

return view.extend({
	load() {
		return Promise.all([
			request.get(L.resource('svg/cpufreq.svg')).then(response => {
				if (!response.ok)
					throw new Error(response.statusText);

				return response.text();
			}),
			L.resolveDefault(fs.list('/sys/devices/system/cpu/cpufreq'), []).then(entries =>
				entries.filter(entry => /^policy\d+$/.test(entry.name))
					.sort((a, b) => +a.name.substring(6) - +b.name.substring(6)))
		]);
	},

	updateGraph(ctx, data) {
		const width = Math.max(5, document.querySelector('#view').offsetWidth - 2);
		const height = 298;
		const step = 5;
		const count = Math.floor(width / step);
		const svg = ctx.svg.firstElementChild;

		for (const row of data) {
			if (row[0] > ctx.timestamp && Number.isFinite(row[1]) && row[1] > 0) {
				ctx.values.push(row[1]);
				ctx.timestamp = row[0];
			}
		}

		ctx.values = ctx.values.slice(-count);

		const values = ctx.values;
		const peak = values.length ? Math.max.apply(Math, values) : null;
		const average = values.length ? values.reduce((sum, value) => sum + value, 0) / values.length : null;
		const maximum = Math.max(100000, Math.ceil((peak || 0) / 100000) * 100000);

		/* The collector stores one sample per second, independently of polling. */
		if (ctx.width !== width) {
			const grid = svg.getElementById('time_grid');
			while (grid.firstChild)
				grid.removeChild(grid.firstChild);

			for (let x = width - step * 60; x > 0; x -= step * 60) {
				const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
				line.setAttribute('x1', x);
				line.setAttribute('y1', 0);
				line.setAttribute('x2', x);
				line.setAttribute('y2', '100%');
				line.setAttribute('style', 'stroke:black;stroke-width:0.1');

				const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
				text.setAttribute('x', x + 5);
				text.setAttribute('y', 15);
				text.setAttribute('style', 'fill:#eee;font-size:9pt;font-family:sans-serif;text-shadow:1px 1px 1px #000');
				text.textContent = '%dm'.format(Math.round((width - x) / step / 60));
				grid.appendChild(line);
				grid.appendChild(text);
			}

			ctx.width = width;
		}

		/* Pad the uncollected part of the window without including it in averages. */
		let points = '0,' + height;
		let y = height;
		for (let i = 0; i < count; i++) {
			const value = values[i - (count - values.length)] || 0;
			y = height - Math.floor(value / maximum * height);
			points += ' ' + (i * step) + ',' + y;
		}
		points += ' ' + width + ',' + y + ' ' + width + ',' + height;
		svg.getElementById('frequency').setAttribute('points', points);

		for (const percent of [25, 50, 75])
			svg.getElementById('label_' + percent).textContent = formatFrequency(maximum * percent / 100);

		ctx.section.querySelector('[data-value="current"]').textContent = formatFrequency(values.length ? values[values.length - 1] : null);
		ctx.section.querySelector('[data-value="average"]').textContent = formatFrequency(average);
		ctx.section.querySelector('[data-value="peak"]').textContent = formatFrequency(peak);
		ctx.section.querySelector('[data-value="scale"]').textContent =
			_('(%d second window, %d second interval)').format(count, 1);
	},

	render([svg, policies]) {
		const tabs = E('div');
		const graphs = [];
		const result = E('div', { 'class': 'cbi-map' }, [
			E('h2', _('CPU Frequency')),
			E('div', { 'class': 'cbi-map-descr' }, _('This page displays the CPU frequency for each scaling policy.')),
			E('div', { 'class': 'cbi-section' }, tabs)
		]);

		if (!policies.length) {
			tabs.appendChild(E('em', _('No CPU frequency policies available.')));
			return result;
		}

		for (const policy of policies) {
			const graph = E('div', {
				'style': 'width:100%;height:300px;border:1px solid #000;background:#fff'
			}, E(svg));
			const section = E('div', {
				'data-tab': policy.name,
				'data-tab-title': policy.name
			}, [
				graph,
				E('div', { 'class': 'right' }, E('small', { 'data-value': 'scale' }, '-')),
				E('br'),
				E('table', { 'class': 'table', 'style': 'width:100%;table-layout:fixed' }, [
					E('tr', { 'class': 'tr' }, [
						E('td', { 'class': 'td right top' }, E('strong', { 'style': 'border-bottom:2px solid #f00' }, _('Current:'))),
						E('td', { 'class': 'td', 'data-value': 'current' }, '-'),
						E('td', { 'class': 'td right top' }, E('strong', _('Average:'))),
						E('td', { 'class': 'td', 'data-value': 'average' }, '-'),
						E('td', { 'class': 'td right top' }, E('strong', _('Peak:'))),
						E('td', { 'class': 'td', 'data-value': 'peak' }, '-')
					])
				])
			]);

			tabs.appendChild(section);
			graphs.push({ policy: policy.name, section: section, svg: graph, values: [], timestamp: 0 });
		}

		ui.tabs.initTabGroup(tabs.childNodes);

		poll.add(() => Promise.all(graphs.map(ctx =>
			L.resolveDefault(callLuciRealtimeStats('cpufreq', ctx.policy), [])
				.then(data => this.updateGraph(ctx, data))
		)), pollInterval);

		return result;
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
