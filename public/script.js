document.addEventListener('DOMContentLoaded', function() {
    const runButtons = document.querySelectorAll('.run-code');
    
    runButtons.forEach(button => {
        button.addEventListener('click', function() {
            const outputId = this.getAttribute('data-output');
            const outputElement = document.getElementById(outputId);
            
            if (outputId === 'hello-world-output') {
                outputElement.textContent = 'Output: Hello, World!';
            } else if (outputId === 'sum-output') {
                outputElement.textContent = 'Output: Sum of 5 and 10 is 15';
            }
        });
    });
});
