const action = document.getElementById('action')
const rows = document.getElementById('bindings')
let controls = []
let capturing = false

function send(name, extra = {}) {
	engine.send('controls.' + name, Object.assign({ action: action.value }, extra))
}

function render() {
	rows.textContent = ''
	const selected = controls.find(item => item.name === action.value)
	if (!selected) return
	selected.bindings.forEach((binding, index) => {
		const row = document.createElement('div')
		row.className = 'binding'
		const text = document.createElement('input')
		text.type = 'text'
		text.value = binding
		row.appendChild(text)
		for (const label of ['Rebind', 'Apply', 'Remove']) {
			const button = document.createElement('button')
			button.textContent = label
			button.disabled = capturing
			button.onclick = () => send(label === 'Apply' ? 'set' : label.toLowerCase(), {
				index, binding: text.value, axes: document.getElementById('axes').checked
			})
			row.appendChild(button)
		}
		rows.appendChild(row)
	})
	for (const button of document.querySelectorAll('.toolbar button')) button.disabled = capturing
}

action.onchange = render
document.getElementById('add').onclick = () => send('add')
document.getElementById('reset').onclick = () => send('reset')
document.getElementById('save').onclick = () => send('save')
document.getElementById('load').onclick = () => send('load')
document.getElementById('resetAll').onclick = () => send('resetAll')

engine.watch('controls', value => {
	controls = value
	const previous = action.value
	action.textContent = ''
	for (const item of controls) {
		const option = document.createElement('option')
		option.value = option.textContent = item.name
		action.appendChild(option)
	}
	action.value = controls.some(item => item.name === previous) ? previous : controls.some(item => item.name === 'Jump') ? 'Jump' : (controls.length ? controls[0].name : '')
	render()
})
engine.watch('capturing', value => {
	capturing = value
	render()
})
