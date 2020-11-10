#pragma once

template <class E> inline E operator| (const E a, const E b) {
	using U = typename std::underlying_type<E>::type;
	return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
}
template <class E> inline E operator|= (E& a, const E b) {
	return a = a | b;
}

template <class E> inline E operator& (const E a, const E b) {
	using U = typename std::underlying_type<E>::type;
	return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
}
